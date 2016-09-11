/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#include <cassert>

#include "moveselection.h"
#include "threaded.h"

namespace {

  enum Stages {
    MAIN_SEARCH, GOOD_CAPTURES, KILLERS, ALL_QUIETS, BAD_CAPTURES,
    EVASION, ALL_EVASIONS,
    QSEARCH_WITH_CHECKS, QCAPTURES_1, CHECKS,
    QSEARCH_WITHOUT_CHECKS, QCAPTURES_2,
    PROBCUT, PROBCUT_CAPTURES,
    RECAPTURE, RECAPTURES,
    STOP
  };

  // Our insertion sort, which is guaranteed to be stable, as it should be
  void insertion_sort(ExtMove* begin, ExtMove* end)
  {
    ExtMove tmp, *p, *q;

    for (p = begin + 1; p < end; ++p)
    {
        tmp = *p;
        for (q = p; q != begin && *(q-1) < tmp; --q)
            *q = *(q-1);
        *q = tmp;
    }
  }

  // pick_best() finds the best move in the range (begin, end) and moves it to
  // the front. It's faster than sorting all the moves in advance when there
  // are few moves, e.g., the possible captures.
  Move pick_best(ExtMove* begin, ExtMove* end)
  {
      std::swap(*begin, *std::max_element(begin, end));
      return *begin;
  }

} // namespace


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions, and some checks) and how important good move
/// ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, Search::Stack* s)
           : pos(p), ss(s), depth(d) {

  assert(d > DEPTH_ZERO);

  Square prevSq = to_sq((ss-1)->currentMove);
  countermove = pos.this_thread()->counterMoves[pos.piece_on(prevSq)][prevSq];

  stage = pos.checkers() ? EVASION : MAIN_SEARCH;
  ttMove = ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE;
  endMoves += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, Square s)
           : pos(p) {

  assert(d <= DEPTH_ZERO);

  if (pos.checkers())
      stage = EVASION;

  else if (d > DEPTH_QS_NO_CHECKS)
      stage = QSEARCH_WITH_CHECKS;

  else if (d > DEPTH_QS_RECAPTURES)
      stage = QSEARCH_WITHOUT_CHECKS;

  else
  {
      stage = RECAPTURE;
      recaptureSquare = s;
      ttm = MOVE_NONE;
  }

  ttMove = ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE;
  endMoves += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Value th)
           : pos(p), threshold(th) {

  assert(!pos.checkers());

  stage = PROBCUT;

  // In ProbCut we generate captures with SEE higher than the given threshold
  ttMove =   ttm
          && pos.pseudo_legal(ttm)
          && pos.capture(ttm)
          && pos.see(ttm) > threshold ? ttm : MOVE_NONE;

  endMoves += (ttMove != MOVE_NONE);
}


/// score() assigns a numerical value to each move in a move list. The moves with
/// highest values will be picked first.
template<>
void MovePicker::score<CAPTURES>() {
  // Winning and equal captures in the main search are ordered by MVV, preferring
  // captures near our home rank. Surprisingly, this appears to perform slightly
  // better than SEE-based move ordering: exchanging big pieces before capturing
  // a hanging piece probably helps to reduce the subtree size.
  // In the main search we want to push captures with negative SEE values to the
  // badCaptures[] array, but instead of doing it now we delay until the move
  // has been picked up, saving some SEE calls in case we get a cutoff.
  for (auto& m : *this)
      m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
               - Value(200 * relative_rank(pos.side_to_move(), to_sq(m)));
}

template<>
void MovePicker::score<QUIETS>() {

  const HistoryStats& history = pos.this_thread()->history;

  const CounterMoveStats* cm = (ss-1)->counterMoves;
  const CounterMoveStats* fm = (ss-2)->counterMoves;
  const CounterMoveStats* f2 = (ss-4)->counterMoves;

  for (auto& m : *this)
      m.value =          history[pos.moved_piece(m)][to_sq(m)]
               + (cm ? 3 * (*cm)[pos.moved_piece(m)][to_sq(m)] : VALUE_ZERO)
               + (fm ? 2 * (*fm)[pos.moved_piece(m)][to_sq(m)] : VALUE_ZERO)
               + (f2 ?     (*f2)[pos.moved_piece(m)][to_sq(m)] : VALUE_ZERO);
}

template<>
void MovePicker::score<EVASIONS>() {
  // Try winning and equal captures ordered by MVV/LVA, then non-captures ordered
  // by history value, then bad captures and quiet moves with a negative SEE ordered
  // by SEE value.
  const HistoryStats& history = pos.this_thread()->history;
  Value see;

  for (auto& m : *this)
      if ((see = pos.see_sign(m)) < VALUE_ZERO)
          m.value = see - HistoryStats::Max; // At the bottom

      else if (pos.capture(m))
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                   - Value(type_of(pos.moved_piece(m))) + HistoryStats::Max
				   + PieceValue[MG][pos.moved_piece(m)] / 16;
      else
          m.value = history[pos.moved_piece(m)][to_sq(m)];
}


/// generate_next_stage() generates, scores, and sorts the next bunch of moves
/// when there are no more moves to try for the current stage.

void MovePicker::generate_next_stage() {

  assert(stage != STOP);

  cur = moves;

  switch (++stage) {

  case GOOD_CAPTURES: case QCAPTURES_1: case QCAPTURES_2:
  case PROBCUT_CAPTURES: case RECAPTURES:
      endMoves = generate<CAPTURES>(pos, moves);
      score<CAPTURES>();
      break;

  case KILLERS:
      killers[0] = ss->killers[0];
      killers[1] = ss->killers[1];
      killers[2] = countermove;
      cur = killers;
      endMoves = cur + 2 + (countermove != killers[0] && countermove != killers[1]);
      break;

  case ALL_QUIETS:
      endGoodQuiets = endMoves = generate<QUIETS>(pos, moves);
      score<QUIETS>();
      if (depth < 3 * ONE_PLY)
          endGoodQuiets = std::partition(cur, endMoves, [](const ExtMove& m) { return m.value > VALUE_ZERO; });
      insertion_sort(cur, endGoodQuiets);
      break;

  case BAD_CAPTURES:
      // Just pick them in reverse order to get correct ordering
      cur = moves + MAX_MOVES - 1;
      endMoves = endBadCaptures;
      break;

  case ALL_EVASIONS:
      endMoves = generate<EVASIONS>(pos, moves);
      if (endMoves - moves > 1)
          score<EVASIONS>();
      break;

  case CHECKS:
      endMoves = generate<QUIET_CHECKS>(pos, moves);
      break;

  case EVASION: case QSEARCH_WITH_CHECKS: case QSEARCH_WITHOUT_CHECKS:
  case PROBCUT: case RECAPTURE: case STOP:
      stage = STOP;
      break;

  default:
      assert(false);
  }
}


/// next_move() is the most important method of the MovePicker class. It returns
/// a new pseudo legal move every time it is called, until there are no more moves
/// left. It picks the move with the biggest value from a list of generated moves
/// taking care not to return the ttMove if it has already been searched.

Move MovePicker::next_move() {

  Move move;

  while (true)
  {
      while (cur == endMoves && stage != STOP)
          generate_next_stage();

      switch (stage) {

      case MAIN_SEARCH: case EVASION: case QSEARCH_WITH_CHECKS:
      case QSEARCH_WITHOUT_CHECKS: case PROBCUT:
          ++cur;
          return ttMove;

      case GOOD_CAPTURES:
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
          {
              if (pos.see_sign(move) >= VALUE_ZERO)
                  return move;

              // Losing capture, move it to the tail of the array
              *endBadCaptures-- = move;
          }
          break;

      case KILLERS:
          move = *cur++;
          if (    move != MOVE_NONE
              &&  move != ttMove
              &&  pos.pseudo_legal(move)
              && !pos.capture(move))
              return move;
          break;

      case ALL_QUIETS:
          move = *cur++;
          if (   move != ttMove
              && move != killers[0]
              && move != killers[1]
              && move != killers[2])
              return move;
          break;

      case BAD_CAPTURES:
          return *cur--;

      case ALL_EVASIONS: case QCAPTURES_1: case QCAPTURES_2:
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
              return move;
          break;

      case PROBCUT_CAPTURES:
           move = pick_best(cur++, endMoves);
           if (move != ttMove && pos.see(move) > threshold)
               return move;
           break;

      case RECAPTURES:
          move = pick_best(cur++, endMoves);
          if (to_sq(move) == recaptureSquare)
              return move;
          break;

      case CHECKS:
          move = *cur++;
          if (move != ttMove)
              return move;
          break;

      case STOP:
          return MOVE_NONE;

      default:
          assert(false);
      }
  }
}
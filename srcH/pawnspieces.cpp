/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#include <algorithm>
#include <cassert>

#include "bitlist.h"
#include "pawnspieces.h"
#include "positioning.h"
#include "threaded.h"

namespace {

  #define V Value
  #define S(mg, eg) make_score(mg, eg)

  // Isolated pawn penalty by opposed flag
  const Score Isolated[2] = { S(45, 40), S(30, 27) };

  // Backward pawn penalty by opposed flag
  const Score Backward[2] = { S(67, 42), S(49, 24) };

  // Connected pawn bonus by opposed, phalanx, twice supported and rank
  Score Connected[2][2][2][RANK_NB];

  // Doubled pawn penalty
  const Score Doubled = S(18,38);

  // Lever bonus by rank
  const Score Lever[RANK_NB] = {
    S( 0,  0), S( 0,  0), S(0, 0), S(0, 0),
    S(20, 20), S(40, 40), S(0, 0), S(0, 0) };

  // Weakness of our pawn shelter in front of the king by [distance from edge][rank]
  const Value ShelterWeakness[][RANK_NB] = {
    { V( 97), V(21), V(26), V(51), V(87), V( 89), V( 99) },
    { V(120), V( 0), V(28), V(76), V(88), V(103), V(104) },
    { V(101), V( 7), V(54), V(78), V(77), V( 92), V(101) },
    { V( 80), V(11), V(44), V(68), V(87), V( 90), V(119) } };

  // Danger of enemy pawns moving toward our king by [type][distance from edge][rank]
  const Value StormDanger[][4][RANK_NB] = {
    { { V( 0),  V(  67), V( 134), V(38), V(32) },
      { V( 0),  V(  57), V( 139), V(37), V(22) },
      { V( 0),  V(  43), V( 115), V(43), V(27) },
      { V( 0),  V(  68), V( 124), V(57), V(32) } },
    { { V(20),  V(  43), V( 100), V(56), V(20) },
      { V(23),  V(  20), V(  98), V(40), V(15) },
      { V(23),  V(  39), V( 103), V(36), V(18) },
      { V(28),  V(  19), V( 108), V(42), V(26) } },
    { { V( 0),  V(   0), V(  75), V(14), V( 2) },
      { V( 0),  V(   0), V( 150), V(30), V( 4) },
      { V( 0),  V(   0), V( 160), V(22), V( 5) },
      { V( 0),  V(   0), V( 166), V(24), V(13) } },
    { { V( 0),  V(-283), V(-281), V(57), V(31) },
      { V( 0),  V(  58), V( 141), V(39), V(18) },
      { V( 0),  V(  65), V( 142), V(48), V(32) },
      { V( 0),  V(  60), V( 126), V(51), V(19) } } };

  // Max bonus for king safety. Corresponds to start position with all the pawns
  // in front of the king and no enemy pawn on the horizon.
  const Value MaxSafetyBonus = V(258);

  #undef S
  #undef V

  template<Color Us>
  Score evaluate(const Position& pos, Pawns::Entry* e) {

    const Color  Them  = (Us == WHITE ? BLACK    : WHITE);
    const Square Up    = (Us == WHITE ? DELTA_N  : DELTA_S);
    const Square Right = (Us == WHITE ? DELTA_NE : DELTA_SW);
    const Square Left  = (Us == WHITE ? DELTA_NW : DELTA_SE);

    const Bitboard CenterMask =  (FileCBB | FileDBB | FileEBB | FileFBB) 
                               & (Rank3BB | Rank4BB | Rank5BB | Rank6BB);

    Bitboard b, neighbours, stoppers, doubled, supported, phalanx;
    Square s;
    bool opposed, lever, connected, backward;
    Score score = SCORE_ZERO;
    const Square* pl = pos.squares<PAWN>(Us);
    const Bitboard* pawnAttacksBB = StepAttacksBB[make_piece(Us, PAWN)];

    Bitboard ourPawns   = pos.pieces(Us  , PAWN);
    Bitboard theirPawns = pos.pieces(Them, PAWN);

    e->passedPawns[Us] = e->pawnAttacksSpan[Us] = 0;
    e->kingSquares[Us] = SQ_NONE;
    e->semiopenFiles[Us] = 0xFF;
    e->pawnAttacks[Us] = shift_bb<Right>(ourPawns) | shift_bb<Left>(ourPawns);

    // Count number of light square pawns with more weight on center pawns using a single popcount
    e->bishopPenalty[Us][WHITE] =  popcount((ourPawns & ~DarkSquares)
                                            | shift_bb<Up>(ourPawns & ~DarkSquares & CenterMask));
    // Same for dark square pawns
    e->bishopPenalty[Us][BLACK] =  popcount((ourPawns &  DarkSquares)
                                            | shift_bb<Up>(ourPawns &  DarkSquares & CenterMask));

    // Loop through all pawns of the current color and score each pawn
    while ((s = *pl++) != SQ_NONE)
    {
        assert(pos.piece_on(s) == make_piece(Us, PAWN));

        File f = file_of(s);

        e->semiopenFiles[Us] &= ~(1 << f);
        e->pawnAttacksSpan[Us] |= pawn_attack_span(Us, s);

        // Flag the pawn
        opposed    = theirPawns & forward_bb(Us, s);
        stoppers   = theirPawns & passed_pawn_mask(Us, s);
        lever      = theirPawns & pawnAttacksBB[s];
        doubled    = ourPawns   & (s + Up);
        neighbours = ourPawns   & adjacent_files_bb(f);
        phalanx    = neighbours & rank_bb(s);
        supported  = neighbours & rank_bb(s - Up);
        connected  = supported | phalanx;

        // A pawn is backward when it is behind all pawns of the same color on the
        // adjacent files and cannot be safely advanced.
        if (!neighbours || lever || relative_rank(Us, s) >= RANK_5)
            backward = false;
        else
        {
            // Find the backmost rank with neighbours or stoppers
            b = rank_bb(backmost_sq(Us, neighbours | stoppers));

            // The pawn is backward when it cannot safely progress to that rank:
            // either there is a stopper in the way on this rank, or there is a
            // stopper on adjacent file which controls the way to that rank.
            backward = (b | shift_bb<Up>(b & adjacent_files_bb(f))) & stoppers;

            assert(!backward || !(pawn_attack_span(Them, s + Up) & neighbours));
        }

        // Passed pawns will be properly scored in evaluation because we need
        // full attack info to evaluate them.
        if (!stoppers && !(ourPawns & forward_bb(Us, s)))
            e->passedPawns[Us] |= s;

        // Score this pawn
        if (!neighbours)
            score -= Isolated[opposed];

        else if (backward)
            score -= Backward[opposed];

        if (connected)
            score += Connected[opposed][!!phalanx][more_than_one(supported)][relative_rank(Us, s)];

        if (doubled)
            score -= Doubled;

        if (lever)
            score += Lever[relative_rank(Us, s)];
    }

    return score;
  }

} // namespace

namespace Pawns {

/// Pawns::init() initializes some tables needed by evaluation. Instead of using
/// hard-coded tables, when makes sense, we prefer to calculate them with a formula
/// to reduce independent parameters and to allow easier tuning and better insight.

void init()
{
  static const int Seed[RANK_NB] = { 0, 6, 15, 10, 57, 75, 135, 258 };

  for (int opposed = 0; opposed <= 1; ++opposed)
      for (int phalanx = 0; phalanx <= 1; ++phalanx)
          for (int apex = 0; apex <= 1; ++apex)
              for (Rank r = RANK_2; r < RANK_8; ++r)
  {
      int v = (Seed[r] + (phalanx ? (Seed[r + 1] - Seed[r]) / 2 : 0)) >> opposed;
      v += (apex ? v / 2 : 0);
      Connected[opposed][phalanx][apex][r] = make_score(3 * v / 2, v);
  }
}


/// Pawns::probe() looks up the current position's pawns configuration in
/// the pawns hash table. It returns a pointer to the Entry if the position
/// is found. Otherwise a new Entry is computed and stored there, so we don't
/// have to recompute all when the same pawns configuration occurs again.

Entry* probe(const Position& pos) {

  Key key = pos.pawn_key();
  Entry* e = pos.this_thread()->pawnsTable[key];

  if (e->key == key)
      return e;

  e->key = key;
  e->score = evaluate<WHITE>(pos, e) - evaluate<BLACK>(pos, e);
  e->asymmetry = popcount(e->semiopenFiles[WHITE] ^ e->semiopenFiles[BLACK]);
  e->openFiles = popcount(e->semiopenFiles[WHITE] & e->semiopenFiles[BLACK]);
  return e;
}


/// Entry::shelter_storm() calculates shelter and storm penalties for the file
/// the king is on, as well as the two adjacent files.

template<Color Us>
Value Entry::shelter_storm(const Position& pos, Square ksq) {

  const Color Them = (Us == WHITE ? BLACK : WHITE);

  enum { NoFriendlyPawn, Unblocked, BlockedByPawn, BlockedByKing };

  Bitboard b = pos.pieces(PAWN) & (in_front_bb(Us, rank_of(ksq)) | rank_bb(ksq));
  Bitboard ourPawns = b & pos.pieces(Us);
  Bitboard theirPawns = b & pos.pieces(Them);
  Value safety = MaxSafetyBonus;
  File center = std::max(FILE_B, std::min(FILE_G, file_of(ksq)));

  for (File f = center - File(1); f <= center + File(1); ++f)
  {
      b = ourPawns & file_bb(f);
      Rank rkUs = b ? relative_rank(Us, backmost_sq(Us, b)) : RANK_1;

      b  = theirPawns & file_bb(f);
      Rank rkThem = b ? relative_rank(Us, frontmost_sq(Them, b)) : RANK_1;

      safety -=  ShelterWeakness[std::min(f, FILE_H - f)][rkUs]
               + StormDanger
                 [f == file_of(ksq) && rkThem == relative_rank(Us, ksq) + 1 ? BlockedByKing  :
                  rkUs   == RANK_1                                          ? NoFriendlyPawn :
                  rkThem == rkUs + 1                                        ? BlockedByPawn  : Unblocked]
                 [std::min(f, FILE_H - f)][rkThem];
  }

  return safety;
}


/// Entry::do_king_safety() calculates a bonus for king safety. It is called only
/// when king square changes, which is about 20% of total king_safety() calls.

template<Color Us>
Score Entry::do_king_safety(const Position& pos, Square ksq) {

  kingSquares[Us] = ksq;
  castlingRights[Us] = pos.can_castle(Us);
  int minKingPawnDistance = 0;

  Bitboard pawns = pos.pieces(Us, PAWN);
  if (pawns)
      while (!(DistanceRingBB[ksq][minKingPawnDistance++] & pawns)) {}

  Value bonus = shelter_storm<Us>(pos, ksq);

  // If we can castle use the bonus after the castling if it is bigger
  if (pos.can_castle(MakeCastling<Us, KING_SIDE>::right))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_G1)));

  if (pos.can_castle(MakeCastling<Us, QUEEN_SIDE>::right))
      bonus = std::max(bonus, shelter_storm<Us>(pos, relative_square(Us, SQ_C1)));

  return make_score(bonus, -16 * minKingPawnDistance);
}

// Explicit template instantiation
template Score Entry::do_king_safety<WHITE>(const Position& pos, Square ksq);
template Score Entry::do_king_safety<BLACK>(const Position& pos, Square ksq);

} // namespace PawnsPieces

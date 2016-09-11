/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef PAWNSPIECES_H_INCLUDED
#define PAWNSPIECES_H_INCLUDED

#include "mixed.h"
#include "positioning.h"
#include "typeskind.h"

namespace Pawns {

/// Pawns::Entry contains various information about a pawn structure. A lookup
/// to the pawn hash table (performed by calling the probe function) returns a
/// pointer to an Entry object.

struct Entry {

  Score pawns_score() const { return score; }
  Bitboard pawn_attacks(Color c) const { return pawnAttacks[c]; }
  Bitboard passed_pawns(Color c) const { return passedPawns[c]; }
  Bitboard pawn_attacks_span(Color c) const { return pawnAttacksSpan[c]; }
  int pawn_asymmetry() const { return asymmetry; }
  int open_files() const { return openFiles; }

  int semiopen_file(Color c, File f) const {
    return semiopenFiles[c] & (1 << f);
  }

  int semiopen_side(Color c, File f, bool leftSide) const {
    return semiopenFiles[c] & (leftSide ? (1 << f) - 1 : ~((1 << (f + 1)) - 1));
  }

  int bishop_penalty(Color c, Square s) const {
    return bishopPenalty[c][!!(DarkSquares & s)];
  }

  template<Color Us>
  Score king_safety(const Position& pos, Square ksq) {
    return  kingSquares[Us] == ksq && castlingRights[Us] == pos.can_castle(Us)
          ? kingSafety[Us] : (kingSafety[Us] = do_king_safety<Us>(pos, ksq));
  }

  template<Color Us>
  Score do_king_safety(const Position& pos, Square ksq);

  template<Color Us>
  Value shelter_storm(const Position& pos, Square ksq);

  Key key;
  Score score;
  Bitboard passedPawns[COLOR_NB];
  Bitboard pawnAttacks[COLOR_NB];
  Bitboard pawnAttacksSpan[COLOR_NB];
  Square kingSquares[COLOR_NB];
  Score kingSafety[COLOR_NB];
  int castlingRights[COLOR_NB];
  int semiopenFiles[COLOR_NB];
  int bishopPenalty[COLOR_NB][COLOR_NB]; // [color][light/dark squares]
  int asymmetry;
  int openFiles;
};

typedef HashTable<Entry, 16384> Table;

void init();
Entry* probe(const Position& pos);

} // namespace Pawns

#endif // #ifndef PAWNSPIECES_H_INCLUDED

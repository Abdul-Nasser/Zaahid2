/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef MATERIAL_H_INCLUDED
#define MATERIAL_H_INCLUDED

#include "endgames.h"
#include "mixed.h"
#include "positioning.h"
#include "typeskind.h"

namespace Material {

/// Material::Entry contains various information about a material configuration.
/// It contains a material imbalance evaluation, a function pointer to a special
/// endgame evaluation function (which in most cases is NULL, meaning that the
/// standard evaluation function will be used), and scale factors.
///
/// The scale factors are used to scale the evaluation score up or down. For
/// instance, in KRB vs KR endgames, the score is scaled down by a factor of 4,
/// which will result in scores of absolute value less than one pawn.

struct Entry {

  Score imbalance() const { return make_score(value, value); }
  Phase game_phase() const { return gamePhase; }
  bool specialized_eval_exists() const { return evaluationFunction != nullptr; }
  Value evaluate(const Position& pos) const { return (*evaluationFunction)(pos); }

  // scale_factor takes a position and a color as input and returns a scale factor
  // for the given color. We have to provide the position in addition to the color
  // because the scale factor may also be a function which should be applied to
  // the position. For instance, in KBP vs K endgames, the scaling function looks
  // for rook pawns and wrong-colored bishops.
  ScaleFactor scale_factor(const Position& pos, Color c) const {
    ScaleFactor sf = scalingFunction[c] ? (*scalingFunction[c])(pos)
                                        :  SCALE_FACTOR_NONE;
    return sf != SCALE_FACTOR_NONE ? sf : ScaleFactor(factor[c]);
  }
  Key key;
  int16_t value;
  uint8_t factor[COLOR_NB];
  EndgameBase<Value>* evaluationFunction;
  EndgameBase<ScaleFactor>* scalingFunction[COLOR_NB]; // Could be one for each
                                                       // side (e.g. KPKP, KBPsKs)
  Phase gamePhase;
};

typedef HashTable<Entry, 8192> Table;

Entry* probe(const Position& pos);

} // namespace Materiel

#endif // #ifndef MATERIEL_H_INCLUDED

/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef EVALUTATION_H_INCLUDED
#define EVALUTATION_H_INCLUDED

#include <string>

#include "typeskind.h"

class Position;

namespace Eval {

const Value Tempo = Value(20);                          // Must be visible to search
extern Color rootColor;                                 // Must be visible to search
extern long Optimism[STRATEGY_NB][TERM_NB][COLOR_NB];   // Must be visible to search

std::string trace(const Position& pos);

template<bool DoTrace = false>
Value evaluate(const Position& pos);
}

#endif // #ifndef EVALUTATION_H_INCLUDED

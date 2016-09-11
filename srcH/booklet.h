/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include <fstream>
#include <string>

#include "mixed.h"
#include "positioning.h"

class PolyglotBook : private std::ifstream {
public:
  PolyglotBook();
 ~PolyglotBook();
  Move probe(const Position& pos, const std::string& fName, bool pickBest);

private:
  template<typename T> PolyglotBook& operator>>(T& n);

  bool open(const char* fName);
  size_t find_first(Key key);

  PRNG rng;
  std::string fileName;
};

#endif // #ifndef BOOKLET_H_INCLUDED

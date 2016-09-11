/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef UCICOMMAND_H_INCLUDED
#define UCICOMMAND_H_INCLUDED

#include <map>
#include <string>

#include "typeskind.h"

class Position;

namespace UCI {

class Option;

/// Custom comparator because UCI options should be case insensitive
struct CaseInsensitiveLess {
  bool operator() (const std::string&, const std::string&) const;
};

/// Our options container is actually a std::map
typedef std::map<std::string, Option, CaseInsensitiveLess> OptionsMap;

/// Option class implements an option as defined by UCI protocol
class Option {

  typedef void (*OnChange)(const Option&);

public:
  Option(OnChange = nullptr);
  Option(bool v, OnChange = nullptr);
  Option(const char* v, OnChange = nullptr);
  Option(int v, int min, int max, OnChange = nullptr);

  Option& operator=(const std::string&);
  void operator<<(const Option&);
  operator int() const;
  operator std::string() const;

private:
  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  std::string defaultValue, currentValue, type;
  int min, max;
  size_t idx;
  OnChange on_change;
};

void init(OptionsMap&);
void loop(int argc, char* argv[]);
std::string value(Value v);
std::string square(Square s);
std::string move(Move m, bool chess960);
std::string pv(const Position& pos, Depth depth, Value alpha, Value beta);
Move to_move(const Position& pos, std::string& str);

} // namespace UCICOMMAND

extern UCI::OptionsMap Options;

#endif // #ifndef UCICOMMAND_H_INCLUDED

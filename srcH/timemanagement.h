/*
  Hypnos, a UCI free chess playing engine 
  Copyright (C) 2016 MZ

  Hypnos is free of charge. You may use and copy it for private purposes.
  Hypnos is distributed only in the hope that it will be useful
  There is no warranty of any kind.
*/

#ifndef TIMEMAN_H_INCLUDED
#define TIMEMAN_H_INCLUDED

#include "mixed.h"
#include "searching.h"
#include "threaded.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.

class TimeManagement {
public:
  void init(Search::LimitsType& limits, Color us, int ply);
  int optimum() const { return optimumTime; }
  int maximum() const { return maximumTime; }
  int elapsed() const { return int(Search::Limits.npmsec ? Threads.nodes_searched() : now() - startTime); }

  int64_t availableNodes; // When in 'nodes as time' mode

private:
  TimePoint startTime;
  int optimumTime;
  int maximumTime;
};

extern TimeManagement Time;

#endif // #ifndef TIMEMANAGEMENT_H_INCLUDED

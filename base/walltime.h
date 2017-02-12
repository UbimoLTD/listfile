// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: tomasz.kaftal@gmail.com (Tomasz Kaftal)
//
// The file provides walltime functionalities for the open source version
// of Supersonic.
#ifndef SUPERSONIC_OPENSOURCE_TIMER_WALLTIME_H_
#define SUPERSONIC_OPENSOURCE_TIMER_WALLTIME_H_

#include <sys/time.h>

#include <string>

#include "base/integral_types.h"

typedef int64 MicrosecondsInt64;

namespace base {

// Time conversion utilities.
static constexpr int64 kNumMillisPerSecond = 1000LL;

static constexpr int64 kNumMicrosPerMilli = 1000LL;
static constexpr int64 kNumMicrosPerSecond = kNumMicrosPerMilli * 1000LL;

inline MicrosecondsInt64 ToMicros(const timespec& ts) {
  return ts.tv_sec * kNumMicrosPerSecond + ts.tv_nsec / 1000;
}

template<clockid_t cid> inline MicrosecondsInt64 GetClockMicros() {
  timespec ts;
  clock_gettime(cid, &ts);
  return ToMicros(ts);
}

inline MicrosecondsInt64 GetThreadTime() {
  return base::GetClockMicros<CLOCK_THREAD_CPUTIME_ID>();
}

}  // namespace base

typedef double WallTime;

// Append result to a supplied string.
// If an error occurs during conversion 'dst' is not modified.
void StringAppendStrftime(std::string* dst,
                          const char* format,
                          time_t when,
                          bool local);

inline std::string LocalTimeNow(const char* format) {
  std::string result;
  StringAppendStrftime(&result, format, time(NULL), true);
  return result;
}

// Similar to the WallTime_Parse, but it takes a boolean flag local as
// argument specifying if the time_spec is in local time or UTC
// time. If local is set to true, the same exact result as
// WallTime_Parse is returned.
bool WallTime_Parse_Timezone(const char* time_spec,
                             const char* format,
                             const struct tm* default_time,
                             bool local,
                             WallTime* result);

// Return current time in seconds as a WallTime.
WallTime WallTime_Now();



// Returns the time since the Epoch measured in microseconds.
inline MicrosecondsInt64 GetCurrentTimeMicros() {
  return base::GetClockMicros<CLOCK_REALTIME>();
}

inline MicrosecondsInt64 GetMonotonicMicros() {
  return base::GetClockMicros<CLOCK_MONOTONIC>();
}

// Returns the number of days from epoch that elapsed until the specified date.
// The date must be in year-month-day format. Returns -1 when the date argument
// is invalid.
int32 GetDaysSinceEpoch(const char* date);

// A CycleClock yields the value of a cycle counter that increments at a rate
// that is approximately constant.
class CycleClock {
 public:
  // Return the value of the counter.
  static inline uint64 Now();

  // returns estimated Hz (or cycles per sec).  The estimation is done with
  // a short calibration process.
  static uint64 CycleFreq();

  static uint64 toUsec(uint64 cycles) { return cycles*1000/(CycleFreq() / 1000);}
  static uint64 toMsec(uint64 cycles) { return cycles/(CycleFreq() / 1000);}

  CycleClock() { Reset(); }

  uint64 Usec() const { return toUsec(Now() - start_); }
  uint64 Msec() const { return toMsec(Now() - start_); }

  void Reset() { start_ = Now(); }
 private:
  uint64 start_;
};

std::string PrintLocalTime(uint64 seconds_epoch, const char* format);

inline std::string PrintLocalTime(uint64 seconds_epoch) {
  return PrintLocalTime(seconds_epoch, "%d/%m/%Y %H:%M:%S %Z");
}

// returns H:M:S representation of the seconds.
std::string GetTimerString(uint64 seconds);
void SleepForMilliseconds(uint32 milliseconds);

namespace base {

// A timer and clock interface using posix CLOCK_MONOTONIC_COARSE clock).
class Timer {
  uint64 start_usec_;

public:
  static MicrosecondsInt64 Usec() {
    return GetClockMicros<CLOCK_MONOTONIC_COARSE>();
  }

  static MicrosecondsInt64 ResolutionUsec() {
    timespec ts;
    clock_getres(CLOCK_MONOTONIC_COARSE, &ts);

    return ToMicros(ts);
  }

  Timer() {
    start_usec_ = Usec();
  }

  uint64 EvalUsec() const { return Usec() - start_usec_; }
};



void SleepMicros(uint32 usec);

// Returns current difference in hours between UTC and the time zone.
// For example, TimezoneDiff("EST") should return -5.
//
int TimezoneDiff(const char* tm_zone);

// Sets up 100usec precision fast timer.
void SetupJiffiesTimer();
void DestroyJiffiesTimer();

// Thread-safe. Very fast 100 microsecond precision monotonic clock.

constexpr uint32 kMicrosToJiffie = 100;
constexpr uint32 kJiffiesToMs = 10;

uint64 GetMonotonicJiffies();

uint64 GetMonotonicMicrosFast();

}  // namespace base

#include "base/cycleclock-inl.h"  // inline method bodies


#endif  // SUPERSONIC_OPENSOURCE_TIMER_WALLTIME_H_

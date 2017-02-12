// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
// Copied from cpp-btree project.
#ifndef _BASE_GTEST_H
#define _BASE_GTEST_H

#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

namespace base {

// Used to avoid compiler optimizations for these benchmarks.
// Just call it with the return value of the function.
template <typename T> void sink_result(const T& t0) {
  volatile T t = t0;
  (void)t;
}

// Returns unique test dir - the same for the run of the process.
// The directory is cleaned automatically if all the tests finish succesfully.
std::string GetTestTempDir();
std::string GetTestTempPath(const std::string& base_name);
}  // namespace base


#define StopBenchmarkTiming state.PauseTiming
#define StartBenchmarkTiming state.ResumeTiming
#define DECLARE_BENCHMARK_FUNC(name, iters)  \
   static void name(benchmark::State& state); \
   void name(benchmark::State& state)

#endif  // _BASE_GTEST_H
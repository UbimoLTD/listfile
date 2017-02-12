// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "base/init.h"
#include "base/gtest.h"
#include "base/logging.h"
#include "base/posix_file.h"

DEFINE_bool(bench, false, "Run benchmarks");

using namespace std;

namespace base {

static char test_path[1024] = {0};

std::string GetTestTempDir() {
  if (test_path[0] == '\0') {
    strcpy(test_path, "/tmp/XXXXXX");
    CHECK(mkdtemp(test_path)) << test_path;
    LOG(INFO) << "Creating test directory " << test_path;
  }
  return test_path;
}

std::string GetTestTempPath(const std::string& base_name) {
  string path = GetTestTempDir();
  path.append("/").append(base_name);
  return path;
}

}  // namespace base


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--bench") == 0) {
      benchmark::Initialize(&argc, argv);
      break;
    }
  }

  MainInitGuard guard(&argc, &argv);
  LOG(INFO) << "Starting tests in " << argv[0];
  int res = RUN_ALL_TESTS();

  if (FLAGS_bench) {
    benchmark::RunSpecifiedBenchmarks();
  }
  if (res == 0 && base::test_path[0]) {
    base::DeleteRecursively(base::test_path);
  }
  return res;
}

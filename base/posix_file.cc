// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//

#include "base/posix_file.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

namespace base {

using namespace std;

// Create a directory.
bool CreateDir(const char* name, int mode) {
  return mkdir(name, mode) == 0;
}

void DeleteRecursively(const char* name) {
  // We don't care too much about error checking here since this is only used
  // in tests to delete temporary directories that are under /tmp anyway.

  // Use opendir()!  Yay!
  // lstat = Don't follow symbolic links.
  struct stat stats;
  if (lstat(name, &stats) != 0) return;

  if (S_ISREG(stats.st_mode)) {
    remove(name);
    return;
  }
  DIR* dir = opendir(name);
  if (dir == nullptr) {
    return;
  }
  string tmp(name);
  while (true) {
    struct dirent* entry = readdir(dir);
    if (entry == NULL) break;
    string entry_name = entry->d_name;
    if (entry_name != "." && entry_name != "..") {
      string item = tmp + "/" + entry_name;
      DeleteRecursively(item.c_str());
    }
  }
  closedir(dir);
  rmdir(name);
}

int64_t FileSize(const char* path) {
  struct stat statbuf;
  if (stat(path, &statbuf) == -1) {
    return -1;
  }
  return statbuf.st_size;
}   // In bytes.


}  // namespace base
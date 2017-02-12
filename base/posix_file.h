// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//

#include <functional>

namespace base {

// Create a directory.
bool CreateDir(const char* name, int mode);

// If "name" is a file, we delete it.  If it is a directory, we
// call DeleteRecursively() for each file or directory (other than
// dot and double-dot) within it, and then delete the directory itself.
// The "dummy" parameters have a meaning in the original version of this
// method but they are not used anywhere in protocol buffers.
void DeleteRecursively(const char* name);

// Returns -1 if the path does not exist or can not be statted.
int64_t FileSize(const char* path);   // In bytes.


}  // namespace base
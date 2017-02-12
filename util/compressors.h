// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#pragma once

#include "base/status.h"

namespace util {
namespace compressors {

// TODO: redesign this interface
// and get rid of register functions because the linker discards unused modules.
enum Method {
  UNKNOWN_METHOD = 0,
  ZLIB_METHOD = 1,
  SNAPPY_METHOD = 2,
  LZ4_METHOD = 3,
  NUM_METHODS = 4
};

const char* MethodName(Method m);

typedef base::Status (*UncompressFunction)(const void* src, size_t len, void* dest,
                                           size_t* uncompress_size);

typedef base::Status (*CompressFunction)(int level, const void* src, size_t len, void* dest,
                                         size_t* compress_size);
typedef size_t (*BoundFunction)(size_t len);


base::Status MaxCompressBound(Method method, size_t src_len, size_t* dest_len);

base::Status GetUncompress(Method method, UncompressFunction* dest);
base::Status GetCompress(Method method, CompressFunction* dest);

namespace internal {

int Register(Method method, BoundFunction func2, CompressFunction cfunc, UncompressFunction ufunc);

void RegisterZlibCompression();

}  // namespace internal

}  // namespace compressors
}  // namespace util

#define REGISTER_COMPRESS(Method, fun1, fun2, fun3) \
  namespace { \
    static int __internal_ ## Method ##_var = \
      ::util::compressors::internal::Register(::util::compressors::Method, fun1, fun2, fun3); \
  }

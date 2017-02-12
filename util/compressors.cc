// Copyright 2015, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//

#include "util/compressors.h"

#include <zlib.h>

#include "base/logging.h"

using base::Status;
using base::StatusCode;

namespace util {
namespace compressors {

extern int dummy_lz4();

namespace {

int dummy_lz4_val = dummy_lz4();

struct Item {
  UncompressFunction uncompr = nullptr;
  CompressFunction compr = nullptr;
  BoundFunction bfun = nullptr;
};

Item& GetItem(Method m) {
  static Item arr[NUM_METHODS];
  return arr[m];
}

size_t BoundFunctionZlib(size_t len) {
  return compressBound(len);
}

Status CompressZlib(int level, const void* src, size_t len, void* dest,
                    size_t* compress_size) {
  // Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level) {
  z_stream stream;
  stream.next_in = (Bytef*)src;
  stream.avail_in = len;
  stream.next_out = (Bytef*)dest;
  stream.avail_out = *compress_size;

  stream.zalloc = 0;
  stream.zfree = 0;
  stream.opaque = (voidpf)0;

  // -15 for writing raw inflate, no headers at all.
  int err = deflateInit2(&stream, level, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);
  if (err != Z_OK)
    goto err_line2;

  err = deflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    deflateEnd(&stream);
    if (err == Z_OK)
      err = Z_BUF_ERROR;
    goto err_line2;
  }
  *compress_size = stream.total_out;

  err = deflateEnd(&stream);
  if (err == Z_OK)
    return Status::OK;

err_line2:
  return Status(zError(err));
}


Status UncompressZlib(const void* src, size_t len, void* dest, size_t* uncompress_size) {
  z_stream stream;
  int err;

  stream.next_in = (Bytef*)src;
  stream.avail_in = (uInt)len;

  stream.next_out = (Bytef*)dest;
  stream.avail_out = *uncompress_size;

  stream.zalloc = nullptr;
  stream.zfree = nullptr;

  err = inflateInit2(&stream, -15);
  if (err != Z_OK)
    goto err_line;

  err = inflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    inflateEnd(&stream);
    if (err == Z_OK)
      err = Z_BUF_ERROR;
    goto err_line;
  }
  *uncompress_size = stream.total_out;

  err = inflateEnd(&stream);
  if (err == Z_OK)
    return Status::OK;

err_line:
  return Status(zError(err));
}

REGISTER_COMPRESS(ZLIB_METHOD, &BoundFunctionZlib, &CompressZlib, &UncompressZlib);

inline Status NotFound() { return Status("Method not found");}

}  // namespace

namespace internal {
void RegisterZlibCompression() {
  internal::Register(ZLIB_METHOD, &BoundFunctionZlib, &CompressZlib, &UncompressZlib);
}
}

base::Status GetUncompress(Method method, UncompressFunction* dest) {
  const Item& i = GetItem(method);
  if (i.uncompr == nullptr) {
    return NotFound();
  }
  *dest = i.uncompr;
  return Status::OK;
}

base::Status MaxCompressBound(Method method, size_t src_len, size_t* dest_len) {
  const Item& i = GetItem(method);
  if (i.bfun == nullptr) {
    return NotFound();
  }
  *dest_len = i.bfun(src_len);
  return Status::OK;
}

base::Status GetCompress(Method method, CompressFunction* dest) {
 const Item& i = GetItem(method);
  if (i.compr == nullptr) {
    return NotFound();
  }
  *dest = i.compr;

  return Status::OK;
}

const char* MethodName(Method m) {
  switch (m) {
    case ZLIB_METHOD: return "ZLIB";
    case SNAPPY_METHOD: return "SNAPPY";
    case LZ4_METHOD: return "LZ4";
    default:;
  }
  return "Unknown";
}

namespace internal {

int Register(Method method, BoundFunction func2, CompressFunction cfunc,
             UncompressFunction ufunc) {
  Item& i = GetItem(method);

  CHECK(i.uncompr == nullptr ||
        (i.uncompr == ufunc && i.compr == cfunc && i.bfun == func2)) << "Method already registered";
  i.uncompr = ufunc;
  i.compr = cfunc;
  i.bfun = func2;

  return 0;
}

}  // namespace internal

}  // namespace compressors
}  // namespace util

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
// File wrapper implementation.
#include "file/file.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <memory>

#include "base/logging.h"
#include "base/macros.h"

using std::string;
using base::Status;
using base::StatusCode;
using strings::Slice;

namespace file {

Status StatusFileError() {
  char buf[1024];
  char* result = strerror_r(errno, buf, sizeof(buf));

  return Status(StatusCode::IO_ERROR, result);
}

namespace {

static size_t read_all(int fd, uint8* buffer, size_t length, size_t offset) {
  uint64 left_to_read = length;
  uint8* curr_buf = buffer;
  while (left_to_read > 0) {
    ssize_t read = pread(fd, curr_buf, left_to_read, offset);
    if (read < 0) {
      return read;
    }
    if (read == 0) {
      return length - left_to_read;
    }
    curr_buf += read;
    offset += read;
    left_to_read -= read;
  }
  return length;
}

// Returns true if a uint64 actually looks like a negative int64. This checks
// if the most significant bit is one.
//
// This function exists because the file interface declares some length/size
// fields to be uint64, and we want to catch the error case where someone
// accidently passes an negative number to one of the interface routines.
inline bool IsUInt64ANegativeInt64(uint64 num) {
  return (static_cast<int64>(num) < 0);
}

// ----------------- LocalFileImpl --------------------------------------------
// Simple file implementation used for local-machine files (mainly temporary)
// only.
class LocalFileImpl : public File {
 public:

  // flags defined at http://man7.org/linux/man-pages/man2/open.2.html
  LocalFileImpl(StringPiece file_name, int flags) : File(file_name), flags_(flags) {
  }

  LocalFileImpl(const LocalFileImpl&) = delete;

  virtual ~LocalFileImpl();

  // Return true if file exists.  Returns false if file does not exist or if an
  // error is encountered.
  // virtual bool Exists() const;

  // File handling methods.
  virtual bool Open();
  // virtual bool Delete();
  virtual bool Close();

  // virtual char* ReadLine(char* buffer, uint64 max_length);
  Status Write(const uint8* buffer, uint64 length, uint64* bytes_written);
  Status Flush();

 protected:
  int fd_ = 0;
  int flags_;
};

LocalFileImpl::~LocalFileImpl() { }

bool LocalFileImpl::Open() {
  if (fd_) {
    LOG(ERROR) << "File already open: " << fd_;
    return false;
  }

  fd_ = open(create_file_name_.c_str(), flags_, 0644);
  if (fd_ < 0) {
    LOG(ERROR) << "Could not open file " << strerror(errno) << " file " << create_file_name_;
    return false;
  }
  return true;
}

bool LocalFileImpl::Close() {
  if (fd_ > 0) {
    close(fd_);
  }
  delete this;
  return true;
}

Status LocalFileImpl::Write(const uint8* buffer, uint64 length, uint64* bytes_written) {
  CHECK_NOTNULL(buffer);
  CHECK(!IsUInt64ANegativeInt64(length));
  uint64 left_to_write = length;
  while (left_to_write > 0) {
    ssize_t written = write(fd_, buffer, left_to_write);
    if (written < 0) {
      return StatusFileError();
    }
    buffer += written;
    left_to_write -= written;
  }

  *bytes_written = length;
  return Status::OK;
}

Status LocalFileImpl::Flush() {
  if (fsync(fd_) < 0) return StatusFileError();
  return Status::OK;
}

}  // namespace

File::File(StringPiece name)
    : create_file_name_(name.ToString()) { }

File::~File() { }



File* Open(StringPiece file_name, OpenOptions opts) {
  int flags = O_CREAT | O_WRONLY | O_CLOEXEC;
  if (opts.append)
    flags |= O_APPEND;
  else
    flags |= O_TRUNC;
  File* ptr = new LocalFileImpl(file_name, flags);
  if (ptr->Open())
    return ptr;
  ptr->Close(); // to delete the object.
  return nullptr;
}

bool Exists(StringPiece fname) {
  return access(fname.data(), F_OK) == 0;
}

bool Delete(StringPiece name) {
  int err;
  if ((err = unlink(name.data())) == 0) {
    return true;
  } else {
    return false;
  }
}

ReadonlyFile::~ReadonlyFile() {
}

constexpr size_t kMaxMmapSize = 1U << 24;  // 16MB

static inline const uint8* MmapFile(int fd, size_t size, off_t offset) {
  return reinterpret_cast<uint8*>(mmap(NULL, std::min(size, kMaxMmapSize), PROT_READ,
                                  MAP_SHARED | MAP_NORESERVE, fd, offset));
}



class PosixMmapReadonlyFile : public ReadonlyFile {
  int fd_;
  const uint8* base_;
  size_t sz_;
  size_t mmap_offs_ = 0;

  size_t mmap_size() const { return std::min(kMaxMmapSize, sz_ - mmap_offs_); }

 public:
  PosixMmapReadonlyFile(int fd, const uint8* base, size_t sz) : fd_(fd), base_(base), sz_(sz) {
  }

  virtual ~PosixMmapReadonlyFile() {
    if (base_) {
      LOG(WARNING) << " ReadonlyFile::Close was not called";
      WARN_IF_ERROR(Close());
    }
  }

  Status Read(size_t offset, size_t length, StringPiece* result, uint8* buffer) override;

  Status Close() override;

  size_t Size() const override {
    return sz_;
  }
};

Status PosixMmapReadonlyFile::Read(
    size_t offset, size_t length, StringPiece* result, uint8* buf) {
  Status s;
  result->clear();
  if (length == 0) return s;

  if (offset > sz_) {
    return Status(StatusCode::RUNTIME_ERROR, "Invalid read range");
  }

  if (offset + length > sz_) {
    length = sz_ - offset;
  }
  size_t end_offs = offset + length;
  static const size_t kPageSize = sysconf(_SC_PAGESIZE);
  size_t mmap_offs = offset & ~(kPageSize - 1);   // align by page boundary.

  // We do not mmap huge blocks (max kMaxMmapSize), so we fallback into reading
  // from the file into the destination buffer.
  if (mmap_offs + kMaxMmapSize < end_offs) {
    ssize_t r = read_all(fd_, buf, length, offset);
    if (r < 0) {
      return StatusFileError();
    }

    *result = StringPiece(buf, length);
    return Status::OK;
  }

  if (offset < mmap_offs_ || end_offs > mmap_offs_ + kMaxMmapSize) {
    if (base_ && munmap(const_cast<uint8*>(base_), mmap_size()) < 0)
      return StatusFileError();

    mmap_offs_ = mmap_offs;

    VLOG(1) << "MMap offset " << mmap_offs_ << " length " << mmap_size();
    base_ = MmapFile(fd_, mmap_size(), mmap_offs_);

    if (base_ == MAP_FAILED) {
      base_ = nullptr;
      LOG(WARNING) << "MAP_FAILED";
      return StatusFileError();
    }
  }
  *result = StringPiece(base_ + offset - mmap_offs_, length);

  return Status::OK;
}


Status PosixMmapReadonlyFile::Close() {
  if (fd_ > 0) {
    posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
    close(fd_);
    fd_ = -1;
  }
  if (base_ && munmap(const_cast<uint8*>(base_), mmap_size()) < 0) {
    return StatusFileError();
  }
  base_ = nullptr;

  return Status::OK;
}

// pread() based access.
class PosixReadFile: public ReadonlyFile {
 private:
  int fd_;
  const size_t file_size_;
  bool drop_cache_;
 public:
  PosixReadFile(int fd, size_t sz, int advice, bool drop) : fd_(fd), file_size_(sz),
          drop_cache_(drop) {
    posix_fadvise(fd_, 0, 0, advice);
  }

  virtual ~PosixReadFile() {
    WARN_IF_ERROR(Close());
  }

  Status Close() override {
    if (fd_) {
      if (drop_cache_)
        posix_fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
      close(fd_);
      fd_ = 0;
    }
    return Status::OK;
  }

  Status Read(size_t offset, size_t length, Slice* result, uint8* buffer) override {
    result->clear();
    if (length == 0) return Status::OK;
    if (offset > file_size_) {
      return Status(StatusCode::RUNTIME_ERROR, "Invalid read range");
    }
    Status s;
    ssize_t r = read_all(fd_, buffer, length, offset);
    if (r < 0) {
      return StatusFileError();
    }
    *result = Slice(buffer, r);
    return s;
  }

  size_t Size() const override { return file_size_; }
};

base::StatusObject<ReadonlyFile*> ReadonlyFile::Open(StringPiece name, const Options& opts) {
  int fd = open(name.data(), O_RDONLY);
  if (fd < 0) {
    return StatusFileError();
  }
  struct stat sb;
  if (fstat(fd, &sb) < 0) {
    close(fd);
    return StatusFileError();
  }
  if (!opts.use_mmap || sb.st_size < 4096) {
    int advice = opts.sequential ? POSIX_FADV_SEQUENTIAL : POSIX_FADV_RANDOM;
    return new PosixReadFile(fd, sb.st_size, advice, opts.drop_cache_on_close);
  }

  // MAP_NORESERVE - we do not want swap space for this mmap. Also we allow
  // overcommitting here (see proc(5)) because this mmap is not allocated from RAM.
  const uint8* base = MmapFile(fd, sb.st_size, 0);

  if (base == MAP_FAILED) {
    VLOG(1) << "Mmap failed " << strerror(errno);
    return StatusFileError();
  }
  return new PosixMmapReadonlyFile(fd, base, sb.st_size);
}


} // namespace file

namespace std {

void default_delete<::file::File>::operator()(::file::File* ptr) const {
  ptr->Close();
}

}  // namespace std

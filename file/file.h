// Copyright 2016, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#pragma once

#include <string>

#include "base/integral_types.h"
#include "strings/stringpiece.h"

#include "base/status.h"
#include "base/port.h"

namespace file {

base::Status StatusFileError();

// ReadonlyFile objects are created via ReadonlyFile::Open() factory function
// and are destroyed via "obj->Close(); delete obj" sequence.
//
class ReadonlyFile {
 protected:
   ReadonlyFile(int retries) : retries_(retries) {}
 public:

  struct Options {
    bool use_mmap = true;
    bool sequential = true;
    bool drop_cache_on_close = true;
    int retries = 1;
    Options() : use_mmap(true) {}
  };

  virtual ~ReadonlyFile();

  // Reads upto length bytes and updates the result to point to the data.
  // May use buffer for storing data. In case, EOF reached sets result.size() < length but still
  // returns Status::OK.
  base::Status Read(size_t offset, size_t length, strings::Slice* result,
                    uint8* buffer) MUST_USE_RESULT;

  // releases the system handle for this file.
  // The object must be deleted.
  base::Status Close();


  virtual size_t Size() const = 0;

  // Factory function that creates the ReadonlyFile object.
  // The ownership is passed to the caller.
  static base::StatusObject<ReadonlyFile*> Open(StringPiece name,
                                                const Options& opts = Options()) MUST_USE_RESULT;
 protected:
  virtual base::Status ReadImpl(size_t offset, size_t length, strings::Slice* result,
                                uint8* buffer) MUST_USE_RESULT = 0;
  virtual base::Status CloseImpl() = 0;
 private:
  const int retries_;
};

// Wrapper class for system functions which handle basic file operations.
// The operations are virtual to enable subclassing, if there is a need for
// different filesystem/file-abstraction support.
class File {
 public:
  // Flush and Close access to a file handle and delete this File
  // object. Returns true on success.
  virtual bool Close() = 0;

  // Opens a file. Should not be called directly.
  virtual bool Open() = 0;

  // Try to write 'length' bytes from 'buffer', returning
  // the number of bytes that were actually written.
  // Return <= 0 on error.
  virtual base::Status Write(const uint8* buffer, uint64 length,
                             uint64* bytes_written) MUST_USE_RESULT = 0 ;

  base::Status Write(StringPiece slice, uint64* bytes_written) MUST_USE_RESULT {
    return Write(slice.ubuf(), slice.size(), bytes_written);
  }

  // Returns the file name given during Create(...) call.
  const std::string& create_file_name() const { return create_file_name_; }

 protected:
  explicit File(const StringPiece create_file_name);

  // Do *not* call the destructor directly (with the "delete" keyword)
  // nor use scoped_ptr; instead use Close().
  virtual ~File();

  // Name of the created file.
  const std::string create_file_name_;
};

struct OpenOptions {
  bool append = false;
};

// Factory method to create a new writable file object. Calls Open on the
// resulting object to open the file.
File* Open(StringPiece file_name, OpenOptions opts = OpenOptions());

// Deletes the file returning true iff successful.
bool Delete(StringPiece name);

bool Exists(StringPiece name);

// Deprecated. Use std::unique_ptr. It should work automatically thanks to default_delete
// specialization below.
class FileCloser {
 public:
  FileCloser() : fp_(nullptr) {}
  // Takes ownership of 'fp' and deletes it upon going out of
  // scope.
  explicit FileCloser(File* fp) : fp_(fp) { }
  File* get() const { return fp_; }
  File& operator*() const { return *fp_; }
  File* operator->() const { return fp_; }
  File* release() {
    File* fp = fp_;
    fp_ = nullptr;
    return fp;
  }
  void reset(File* new_fp) {
    if (fp_) {
      fp_->Close();
    }
    fp_ = new_fp;
  }
  bool Close() {
    return fp_ ? release()->Close() : true;
  }
  // Delete (unlink, remove) the underlying file.
  ~FileCloser() { reset(nullptr); }

 private:
  File* fp_;
  FileCloser(const FileCloser&) = delete;
};

bool IsInS3Namespace(StringPiece name);

struct StatShort {
  std::string name;
  time_t last_modified;
  off_t size;
};


} // namespace file

namespace std {

template<typename T > struct default_delete;

template <> class default_delete<::file::File> {
public:
  void operator()(::file::File* ptr) const;
};

}  // namespace std

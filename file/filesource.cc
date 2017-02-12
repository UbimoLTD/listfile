// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "file/filesource.h"

#include "base/logging.h"
#include "file/file.h"
#include "strings/split.h"
#include "strings/strip.h"
#include "util/bzip_source.h"
#include "util/zlib_source.h"

namespace file {

using strings::Slice;
using strings::SkipWhitespace;
using base::Status;
using namespace std;

Source::Source(ReadonlyFile* file,  Ownership ownership, uint32 buffer_size)
 : BufferredSource(buffer_size), file_(file), ownership_(ownership) {
}

Source::~Source() {
  if (ownership_ == TAKE_OWNERSHIP) {
    CHECK(file_->Close().ok());
    delete file_;
  }
}

Status Source::SkipPos(uint64 offset) {
  offset_ += offset;
  return Status::OK;
}

bool Source::RefillInternal() {
  uint32 refill = available_to_refill();
  strings::Slice result;
  status_ = file_->Read(offset_, refill, &result, peek_pos_ + avail_peek_);
  if (!status_.ok()) {
    return true;
  }
  if (!result.empty()) {
    if (result.ubuf() != peek_pos_ + avail_peek_) {
      memcpy(peek_pos_ + avail_peek_, result.ubuf(), result.size());
    }
    avail_peek_ += result.size();
    offset_ += result.size();
  }

  return result.size() < refill;
}

util::Source* Source::Uncompressed(ReadonlyFile* file, uint32 buffer_size) {
  Source* first = new Source(file, TAKE_OWNERSHIP, buffer_size);
  if (util::BzipSource::IsBzipSource(first))
    return new util::BzipSource(first, TAKE_OWNERSHIP);
  if (util::ZlibSource::IsZlibSource(first))
    return new util::ZlibSource(first, TAKE_OWNERSHIP);
  return first;
}

Sink::~Sink() {
  if (ownership_ == TAKE_OWNERSHIP)
    CHECK(file_->Close());
}

base::Status Sink::Append(strings::Slice slice) {
  uint64 bytes_written = 0;
  return file_->Write(slice.ubuf(), slice.size(), &bytes_written);
}


LineReader::LineReader() : ownership_(TAKE_OWNERSHIP) {}

LineReader::LineReader(const std::string& fl) : LineReader() {
  CHECK(Open(fl));
}

bool LineReader::Open(const std::string& filename) {
  auto res = ReadonlyFile::Open(filename);
  if (!res.ok()) {
    LOG(ERROR) << "Failed to open " << filename << ": " << res.status;
    return false;
  }
  source_ = file::Source::Uncompressed(res.obj);
  return true;
}

LineReader::~LineReader() {
  if (ownership_ == TAKE_OWNERSHIP) {
    delete source_;
  }
}

bool LineReader::Next(std::string* result) {
  CHECK_NOTNULL(source_);
  result->clear();
  bool eof = false;
  while (true) {
    Slice s = source_->Peek();
    if (s.empty()) { eof = true; break; }

    size_t eol = s.find(0xA); // Search for \n
    if (eol != Slice::npos) {
      uint32 skip = eol + 1;
      if (eol > 0 && s[eol - 1] == 0xD)
        --eol;
      result->append(reinterpret_cast<const char*>(s.data()), eol);
      source_->Skip(skip);
      ++line_num_;
      break;
    }
    result->append(reinterpret_cast<const char*>(s.data()), s.size());
    source_->Skip(s.size());
  }
  return !(eof && result->empty());
}

CsvReader::CsvReader(const std::string& filename,
                     std::function<void(const std::vector<StringPiece>&)> row_cb)
    : row_cb_(row_cb) {
  is_valid_ = reader_.Open(filename);
}

void CsvReader::SkipHeader(unsigned rows) {
  if (!is_valid_) {
    return;
  }
  string tmp;
  for (unsigned i = 0; i < rows; ++i) {
    if (!reader_.Next(&tmp))
      return;
  }
}

void CsvReader::Run() {
  if (!is_valid_) {
    return;
  }
  string line;
  vector<char*> v;
  vector<StringPiece> v2;
  while (reader_.Next(&line)) {
    StripWhiteSpace(&line);
    if (line.empty())
      continue;
    v.clear();
    SplitCSVLineWithDelimiter(&line.front(), ',',  &v);
    v2.assign(v.begin(), v.end());
    row_cb_(v2);
  }
}

}  // namespace file
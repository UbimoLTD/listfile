// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "file/filesource.h"

#include "base/logging.h"
#include "file/file.h"
#include "strings/split.h"
#include "strings/strip.h"
#include "strings/util.h"
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
  // This code handles the following line termination cases:
  // 1) \n - linuxes and unixes and new macs
  // 2) \r\n - windows
  // 3) \r - old macs (it was seen in some csvs of ours and is therefore supported)
  // note: \r\n is always treated as windows and not as "one old mac and one linux"
  CHECK_NOTNULL(source_);
  result->clear();
  bool eof = false;
  while (true) {
    Slice s = source_->Peek();
    if (s.empty()) {
      eof = true;
      break;
    }

    // ignore_newline_at_begin_ is only turned on if our last read got a '\r',
    // this flag's purpose is to solve the case where '\r\n' is broken by buffering
    if (ignore_newline_at_begin_) {
      ignore_newline_at_begin_ = false;
      if (s[0] == '\n') {
        source_->Skip(1);
        continue;
      }
    }

    size_t eol = 0;
    while (eol < s.size() && s[eol] != 0xD && s[eol] != 0xA) // search for \r or \n
      ++eol;
    if (eol != s.size()) {
      uint32 skip = eol + 1;
      if (s[eol] == 0xD) {
        if (eol + 1 == s.size())           // if we got a '\r' at buffer's end, make sure to skip
          ignore_newline_at_begin_ = true; // a '\n' if it appears in the beginning of a buffer
        else if (s[eol + 1] == 0xA)
          ++skip;                          // otherwise, just do a normal skip
      }
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

void LineReader::TEST_set_ignore_newline_at_begin(bool value) {
  ignore_newline_at_begin_ = value;
}

CsvReader::CsvReader(const std::string& filename,
                     std::function<void(const std::vector<StringPiece>&)> row_cb, char delimiter)
    : row_cb_(row_cb), delimiter_(delimiter) {
  is_valid_ = reader_.Open(filename);
}

void CsvReader::SkipHeader(unsigned rows) {
  if (!is_valid_) {
    return;
  }
  string tmp;
  unsigned i = 0;
  while (i < rows && reader_.Next(&tmp)) {
    if (!skip_hash_mark_ || !strings::HasPrefixString(tmp, "#"))
      i++;
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
    if (line.empty() || (skip_hash_mark_ && line[0] == '#'))
      continue;
    v.clear();
    SplitCSVLineWithDelimiter(&line.front(), delimiter_,  &v);
    v2.assign(v.begin(), v.end());
    row_cb_(v2);
  }
}

}  // namespace file

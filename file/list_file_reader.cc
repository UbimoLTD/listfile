// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "file/list_file.h"

#include <cstdio>
#include <google/protobuf/descriptor.h>
#include <snappy-c.h>

#include "base/commandlineflags.h"
#include "util/coding/fixed.h"
#include "util/coding/varint.h"
#include "util/crc32c.h"
#include "util/compressors.h"


DEFINE_bool(list_file_use_mmap, true, "");

namespace file {

namespace gpb = ::google::protobuf;

using base::Status;
using base::StatusCode;
using file::ReadonlyFile;
using strings::Slice;
using std::string;
using namespace ::util;
using namespace list_file;

ListReader::ListReader(file::ReadonlyFile* file, Ownership ownership, bool checksum,
                       CorruptionReporter reporter)
  : file_(file), ownership_(ownership), reporter_(reporter),
    checksum_(checksum) {
}

ListReader::ListReader(StringPiece filename, bool checksum, CorruptionReporter reporter)
    : ownership_(TAKE_OWNERSHIP), reporter_(reporter), checksum_(checksum) {
  ReadonlyFile::Options opts;
  opts.use_mmap = FLAGS_list_file_use_mmap;
  auto res = ReadonlyFile::Open(filename, opts);
  CHECK(res.ok()) << res.status << ", file name: " << filename;
  file_ = res.obj;
  CHECK(file_) << filename;
}

ListReader::~ListReader() {
  if (ownership_ == TAKE_OWNERSHIP) {
    auto st = file_->Close();
    if (!st.ok()) {
      LOG(WARNING) << "Error closing file, status " << st;
    }
    delete file_;
  }
}

bool ListReader::GetMetaData(std::map<std::string, std::string>* meta) {
  if (!ReadHeader()) return false;
  *meta = meta_;
  return true;
}

bool ListReader::ReadRecord(Slice* record, std::string* scratch) {
  if (!ReadHeader()) return false;

  scratch->clear();
  record->clear();
  bool in_fragmented_record = false;
  Slice fragment;

  while (true) {
    if (array_records_ > 0) {
      uint32 item_size = 0;
      const uint8* aend = reinterpret_cast<const uint8*>(array_store_.end());
      const uint8* item_ptr = Varint::Parse32WithLimit(array_store_.ubuf(), aend,
                                                       &item_size);
      const uint8* next_rec_ptr = item_ptr + item_size;
      if (item_ptr == nullptr || next_rec_ptr > aend) {
        ReportCorruption(array_store_.size(), "invalid array record");
        array_records_ = 0;
      } else {
        read_header_bytes_ += item_ptr - array_store_.ubuf();
        array_store_.remove_prefix(next_rec_ptr - array_store_.ubuf());
        *record = StringPiece(item_ptr, item_size);
        read_data_bytes_ += item_size;
        --array_records_;
        return true;
      }
    }
    const unsigned int record_type = ReadPhysicalRecord(&fragment);
    switch (record_type) {
      case kFullType:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "partial record without end(1)");
        } else {
          scratch->clear();
          *record = fragment;

          return true;
        }
      case kFirstType:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "partial record without end(2)");
        }
        scratch->assign(fragment.as_string());
        in_fragmented_record = true;

        break;

      case kMiddleType:
        if (!in_fragmented_record) {
          ReportCorruption(fragment.size(),
                           "missing start of fragmented record(1)");
        } else {
          scratch->append(fragment.data(), fragment.size());
        }
        break;

      case kLastType:
        if (!in_fragmented_record) {
          ReportCorruption(fragment.size(),
                           "missing start of fragmented record(2)");
        } else {
          scratch->append(fragment.data(), fragment.size());
          *record = Slice(*scratch);
          read_data_bytes_ += record->size();
          // last_record_offset_ = prospective_record_offset;
          return true;
        }
        break;
      case kArrayType: {
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "partial record without end(4)");
        }
        uint32 array_records = 0;
        const uint8* array_ptr = Varint::Parse32WithLimit(fragment.ubuf(),
              fragment.ubuf() + fragment.size(), &array_records);
        if (array_ptr == nullptr || array_records == 0) {
          ReportCorruption(fragment.size(), "invalid array record");
        } else {
          read_header_bytes_ += array_ptr - fragment.ubuf();
          array_records_ = array_records;
          array_store_ = StringPiece(array_ptr, fragment.end() - strings::charptr(array_ptr));
          VLOG(2) << "Read array with count " << array_records;
        }
      }
      break;
      case kEof:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "partial record without end(3)");
          scratch->clear();
        }
        return false;
      case kBadRecord:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "error in middle of record");
          in_fragmented_record = false;
          scratch->clear();
        }
        break;
      default: {
        char buf[40];
        snprintf(buf, sizeof(buf), "unknown record type %u", record_type);
        ReportCorruption(
            (fragment.size() + (in_fragmented_record ? scratch->size() : 0)),
            buf);
        in_fragmented_record = false;
        scratch->clear();
      }
    }
  }
  return true;
}

static const uint8* DecodeString(const uint8* ptr, const uint8* end, string* dest) {
  if (ptr == nullptr) return nullptr;
  uint32 string_sz = 0;
  ptr = Varint::Parse32WithLimit(ptr, end, &string_sz);
  if (ptr == nullptr || ptr + string_sz > end)
    return nullptr;
  const char* str = reinterpret_cast<const char*>(ptr);
  dest->assign(str, str + string_sz);
  return ptr + string_sz;
}

Status list_file::HeaderParser::Parse(
  file::ReadonlyFile* file, std::map<std::string, std::string>* meta) {

  uint8 buf[kListFileHeaderSize];
  strings::Slice result;
  RETURN_IF_ERROR(file->Read(0, kListFileHeaderSize, &result, buf));

  offset_ = kListFileHeaderSize;
  if (result.size() != kListFileHeaderSize ||
      !result.starts_with(Slice(kMagicString, kMagicStringSize)) ||
      result[kMagicStringSize] == 0 || result[kMagicStringSize] > 100 ) {
    return Status("Invalid header");
  }

  unsigned block_factor = result[kMagicStringSize];
  if (result[kMagicStringSize + 1] == kMetaExtension) {
    uint8 meta_header[8];
    RETURN_IF_ERROR(file->Read(offset_, sizeof meta_header, &result, meta_header));

    offset_ += result.size();

    uint32 length = coding::DecodeFixed32(result.ubuf() + 4);
    uint32 crc = crc32c::Unmask(coding::DecodeFixed32(result.ubuf()));

    std::unique_ptr<uint8[]> meta_buf(new uint8[length]);
    RETURN_IF_ERROR(file->Read(offset_, length, &result, meta_buf.get()));
    CHECK_EQ(result.size(), length);

    offset_ += length;
    uint32 actual_crc = crc32c::Value(result.ubuf(), result.size());
    if (crc != actual_crc) {
      LOG(ERROR) << "Corrupted meta data " << actual_crc << " vs2 " << crc;
      return Status("Bad meta crc");
    }

    const uint8* end = reinterpret_cast<const uint8*>(result.end());
    const uint8* ptr = Varint::Parse32WithLimit(result.ubuf(), end, &length);
    for (uint32 i = 0; i < length; ++i) {
      string key, val;
      ptr = DecodeString(ptr, end, &key);
      ptr = DecodeString(ptr, end, &val);
      if (ptr == nullptr) {
        return Status("Bad meta crc");
      }
      meta->emplace(std::move(key), std::move(val));
    }
  }
  block_multiplier_ = block_factor;

  return Status::OK;
}

bool ListReader::ReadHeader() {
  if (block_size_ != 0) return true;
  if (eof_) return false;

  list_file::HeaderParser parser;
  Status status = parser.Parse(file_, &meta_);

  if (!status.ok()) {
    LOG(ERROR) << "Error reading header " << status;
    ReportDrop(file_->Size(), status);
    eof_ = true;
    return false;
  }

  file_offset_ = read_header_bytes_ = parser.offset();
  block_size_ = parser.block_multiplier() * kBlockSizeFactor;

  CHECK_GT(block_size_, 0);
  backing_store_.reset(new uint8[block_size_]);
  uncompress_buf_.reset(new uint8[block_size_]);

  return true;
}

void ListReader::ReportCorruption(size_t bytes, const string& reason) {
  ReportDrop(bytes, Status(base::StatusCode::IO_ERROR, reason));
}

void ListReader::ReportDrop(size_t bytes, const Status& reason) {
  LOG(ERROR) << "ReportDrop: " << bytes << " "
             << " block buffer_size " << block_buffer_.size() << ", reason: " << reason;
  if (reporter_ /*&& end_of_buffer_offset_ >= initial_offset_ + block_buffer_.size() + bytes*/) {
    reporter_(bytes, reason);
  }
}

using strings::charptr;

unsigned int ListReader::ReadPhysicalRecord(Slice* result) {
  while (true) {
    // Should be <= but due to bug in ListWriter we leave it as < until all the prod files are
    // replaced.
    if (block_buffer_.size() < kBlockHeaderSize) {
      if (!eof_) {
        size_t fsize = file_->Size();
        size_t length = file_offset_ + block_size_ <= fsize ? block_size_ : fsize - file_offset_;
        Status status = file_->Read(file_offset_, length, &block_buffer_, backing_store_.get());
        // end_of_buffer_offset_ += read_size;
        VLOG(2) << "read_size: " << block_buffer_.size() << ", status: " << status;
        if (!status.ok()) {
          ReportDrop(length, status);
          eof_ = true;
          return kEof;
        }
        file_offset_ += block_buffer_.size();
        if (file_offset_ >= fsize) {
          eof_ = true;
        }
        continue;
      } else if (block_buffer_.empty()) {
        // End of file
        return kEof;
      } else {
        size_t drop_size = block_buffer_.size();
        block_buffer_.clear();
        ReportCorruption(drop_size, "truncated record at end of file");
        return kEof;
      }
    }

    // Parse the header
    const uint8* header = block_buffer_.ubuf();
    const uint8 type = header[8];
    uint32 length = coding::DecodeFixed32(header + 4);
    read_header_bytes_ += kBlockHeaderSize;

    if (length == 0 && type == kZeroType) {
      size_t bs = block_buffer_.size();
      block_buffer_.clear();
      // Handle the case of when mistakenly written last kBlockHeaderSize bytes as empty record.
      if (bs != kBlockHeaderSize) {
        LOG(ERROR) << "Bug reading list file " << bs;
        return kBadRecord;
      }
      continue;
    }

    if (length + kBlockHeaderSize > block_buffer_.size()) {
      VLOG(1) << "Invalid length " << length << " file offset " << file_offset_
              << " block size " << block_buffer_.size() << " type " << int(type);
      size_t drop_size = block_buffer_.size();
      block_buffer_.clear();
      ReportCorruption(drop_size, "bad record length or truncated record at eof.");
      return kBadRecord;
    }

    const uint8* data_ptr = header + kBlockHeaderSize;
    // Check crc
    if (checksum_) {
      uint32_t expected_crc = crc32c::Unmask(coding::DecodeFixed32(header));
      // compute crc of the record and the type.
      uint32_t actual_crc = crc32c::Value(data_ptr - 1, 1 + length);
      if (actual_crc != expected_crc) {
        // Drop the rest of the buffer since "length" itself may have
        // been corrupted and if we trust it, we could find some
        // fragment of a real log record that just happens to look
        // like a valid log record.
        size_t drop_size = block_buffer_.size();
        block_buffer_.clear();
        ReportCorruption(drop_size, "checksum mismatch");
        return kBadRecord;
      }
    }
    uint32 record_size = length + kBlockHeaderSize;
    block_buffer_.remove_prefix(record_size);

    if (type & kCompressedMask) {
      if (!Uncompress(data_ptr, &length)) {
        ReportCorruption(record_size, "Uncompress failed.");
        return kBadRecord;
      }
      data_ptr = uncompress_buf_.get();
    }

    *result = Slice(data_ptr, length);
    return type & 0xF;
  }
}

bool ListReader::Uncompress(const uint8* data_ptr, uint32* size) {
  uint8 method = *data_ptr++;
  VLOG(2) << "Uncompress " << int(method) << " with size " << *size;

  uint32 inp_sz = *size - 1;

  if (kCompressionSnappy == method) {
    size_t uncompress_size = block_size_;
    snappy_status st = snappy_uncompress(charptr(data_ptr),
                                         inp_sz, charptr(uncompress_buf_.get()),
                                         &uncompress_size);
    if (st != SNAPPY_OK) {
      return false;
    }
    *size = uncompress_size;
    return true;
  }

  compressors::UncompressFunction uncompr_func = nullptr;
  compressors::Method cm;
  switch (method) {
    case kCompressionZlib: cm = compressors::ZLIB_METHOD;
    break;
    case kCompressionLZ4: cm = compressors::LZ4_METHOD;
    break;
    default:
      LOG(ERROR) << "Unknown compression " << method;
      return false;
  }

  Status status = compressors::GetUncompress(cm, &uncompr_func);
  if (!status.ok()) {
    LOG(ERROR) << "Could not find uncompress method " << compressors::MethodName(cm);
    return false;
  }
  size_t uncompress_size = block_size_;
  status = uncompr_func(data_ptr, inp_sz, uncompress_buf_.get(), &uncompress_size);
  if (!status.ok()) {
    VLOG(1) << "Uncompress error: " << status;
    return false;
  }

  *size = uncompress_size;
  return true;
}

namespace internal {
void ReadProtoRecordsImpl(ListReader *reader_p,
                          bool(*parse_and_cb)(strings::Slice&&, void *cb2),
                          void *cb2,
                          const gpb::Descriptor *desc,
                          bool need_metadata,
                          const StringPiece *name) {
  ListReader& reader = *reader_p;
  std::string record_buf;
  strings::Slice record;
  std::map<std::string, std::string> metadata;
  std::string name_suffix = name ? StrCat(", path: ", *name) : "";
  bool has_metadata = reader.GetMetaData(&metadata) && metadata.count(file::kProtoTypeKey);
  CHECK(has_metadata || !need_metadata) << "Metadata requested but not found" << name_suffix;
  CHECK(!has_metadata || metadata[file::kProtoTypeKey] == desc->full_name())
    << "Type mismatch between " << metadata[file::kProtoTypeKey]
    << " and " << desc->full_name() << name_suffix;
  while (reader.ReadRecord(&record, &record_buf))
    CHECK(parse_and_cb(std::move(record), cb2)) << "size: " << record.size()
      << name_suffix;
}
}

}  // namespace file

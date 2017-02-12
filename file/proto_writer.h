// Copyright 2013, Ubimo.com .  All rights reserved.
// Author: Roman Gershman (roman@ubimo.com)
//
#ifndef _PROTO_WRITER_H
#define _PROTO_WRITER_H

#include <memory>

#include "strings/stringpiece.h"
#include "base/status.h"
#include "base/arena.h"

namespace google {
namespace protobuf {
class Descriptor;
class MessageLite;
}  // namespace protobuf
}  // namespace google

namespace util {
class DiskTable;
class Sink;
}  // namespace util

namespace file {
class ListWriter;

extern const char kProtoSetKey[];
extern const char kProtoTypeKey[];

namespace sstable {
class TableBuilder;
}  // namespace sstable

class ProtoWriter {
  std::unique_ptr<ListWriter> writer_;
  // std::unique_ptr<util::DiskTable> table_;
  std::unique_ptr<util::Sink> sink_;
  std::unique_ptr<sstable::TableBuilder> table_builder_;

  typedef std::pair<strings::Slice, strings::Slice> KVSlice;
  std::vector<KVSlice> k_v_vec_;
  const ::google::protobuf::Descriptor* dscr_;
  base::Arena arena_;

  bool was_init_ = false;
  uint32 entries_per_shard_ = 0;
  uint32 shard_index_ = 0;
  std::string base_name_, fd_set_str_;
public:
  enum Format {LIST_FILE, SSTABLE};

  struct Options {
    Format format;

    enum CompressMethod {SNAPPY_COMPRESS = 1, ZLIB_COMPRESS = 2, LZ4_COMPRESS = 3} compress_method
          = LZ4_COMPRESS;
    uint8 compress_level = 1;

    // if max_entries_per_file > 0 then
    // ProtoWriter uses filename as prefix for generating upto 10000 shards of data when each
    // contains upto max_entries_per_file entries.
    // The file name will be concatenated with "-%04d.lst" suffix for each shard.
    uint32 max_entries_per_file = 0;

    // Whether to append to the existing file or otherwrite it.
    bool append = false;

    Options() : format(LIST_FILE) {}
  };

  ProtoWriter(StringPiece filename, const ::google::protobuf::Descriptor* dscr,
              Options opts = Options());

  ~ProtoWriter();

  base::Status Add(const ::google::protobuf::MessageLite& msg);

  // Used for key-value tables.
  base::Status Add(strings::Slice key, const ::google::protobuf::MessageLite& msg);

  base::Status Flush();

  const ListWriter* writer() const { return writer_.get();}

 private:
  Options options_;
};


}  // namespace file

#endif  // _PROTO_WRITER_H

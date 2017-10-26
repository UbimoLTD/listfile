#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <string>
#include "file/file.h"
#include "file/proto_writer.h"

// When running unittests, get the directory containing the source code.
std::string TestSourceDir();

// When running unittests, get a directory where temporary files may be
// placed.
std::string TestTempDir();


namespace file {

using std::string;

class NullFile : public File {
 public:
  NullFile() : File("NullFile") {}
  virtual bool Close() override { return true; }
  virtual bool Open() override { return true; }
  virtual base::Status Read(size_t , uint8* , size_t* ) {
    return base::Status::OK;
  }
  base::Status Write(const uint8* ,uint64,  uint64* ) override { return base::Status::OK; }
};

class ReadonlyStringFile : public ReadonlyFile {
  string contents_;
public:
  ReadonlyStringFile(const string& str, int retries) : ReadonlyFile(retries), contents_(str)  {}

  size_t Size() const override { return contents_.size(); }
protected:
  // Reads upto length bytes and updates the result to point to the data.
  // May use buffer for storing data.
  base::Status ReadImpl(size_t offset, size_t length, strings::Slice* result,
                        uint8* buffer) override;

  // releases the system handle for this file.
  base::Status CloseImpl() override { return base::Status::OK; }


};

}  // namespace file


template <class MessageList>
base::Status CreateIndexFile(const MessageList& list, const std::string& file_name) {
  file::ProtoWriter writer(file_name, MessageList::value_type::descriptor());
  for (const auto& message : list) {
    RETURN_IF_ERROR(writer.Add(message));
  }
  return writer.Flush();
}

#endif  // TEST_UTIL_H

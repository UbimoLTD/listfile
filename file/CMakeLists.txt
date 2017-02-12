add_library(file meta_map_block.cc)
target_link_libraries(file status_proto)

add_library(list_file list_file.cc list_file_reader.cc file.cc filesource.cc file_util.cc meta_map_block.cc proto_writer.cc)
target_link_libraries(list_file base coding gflags glog protobuf snappy sstable strings util)

add_executable(list_file_test list_file_test.cc)
target_link_libraries(list_file_test list_file gtest_main benchmark)

add_subdirectory(sstable)

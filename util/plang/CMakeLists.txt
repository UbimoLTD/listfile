cxx_proto_lib(addressbook)
add_library(plang plang.cc)
target_link_libraries(plang protobuf math plang_parser_bison)

flex_lib(plang_scanner)

bison_lib(plang_parser)
target_link_libraries(plang_parser_bison strings plang plang_scanner_flex glog)

add_executable(plang_test plang_test.cc)
target_link_libraries(plang_test gtest_main gflags addressbook_proto plang_parser_bison plang strings)

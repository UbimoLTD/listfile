add_executable(pprint pprint.cc)
target_link_libraries(pprint list_file plang_parser_bison pprint_utils pb2json)

add_library(pprint_utils pprint_utils.cc)
target_link_libraries(pprint_utils protobuf strings)

add_executable(pprint_utils_test pprint_utils_test.cc)
target_link_libraries(pprint_utils_test pprint_utils pprint_utils_test_proto gtest_main gflags)

cxx_proto_lib(pprint_utils_test)

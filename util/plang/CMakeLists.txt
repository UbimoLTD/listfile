add_library(plang plang.cc)
target_link_libraries(plang protobuf math plang_parser_bison)

flex_lib(plang_scanner)

bison_lib(plang_parser)
target_link_libraries(plang_parser_bison strings plang plang_scanner_flex glog)

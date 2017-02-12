add_library(json json_parser.cc)
target_link_libraries(json strings)

add_library(pb2json pb2json.cc)
target_link_libraries(pb2json json protobuf base strings)
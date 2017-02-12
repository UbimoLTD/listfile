cxx_proto_lib(points)

add_executable(points points.cc)
target_link_libraries(points list_file points_proto)
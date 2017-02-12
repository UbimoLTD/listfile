add_library(util bzip_source.cc compressors.cc crc32c.cc lz4_compressor.cc proc_stats.cc sinksource.cc zlib_source.cc sp_task_pool.cc)
target_link_libraries(util bz2 glog z strings status_proto)

add_subdirectory(coding)
add_subdirectory(json)
add_subdirectory(math)
add_subdirectory(plang)
add_subdirectory(tools)

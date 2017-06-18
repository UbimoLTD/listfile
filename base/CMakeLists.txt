find_package(Threads)

cxx_proto_lib(status PY)

add_library(base arena.cc hash.cc init.cc posix_file.cc pthread_utils.cc random.cc status.cc walltime.cc)
add_dependencies(base gperf_project gflags_project)
target_link_libraries(base cityhash glog status_proto xxhash ${CMAKE_THREAD_LIBS_INIT} gflags)

add_library(gtest_main gtest_main.cc)
target_link_libraries(gtest_main gtest base benchmark)

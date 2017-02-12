include(CMakeParseArguments)
include(ExternalProject)

find_package(Threads REQUIRED)

set(THIRD_PARTY_LIB_DIR "${THIRD_PARTY_DIR}/libs")
set(THIRD_PARTY_CXX_FLAGS "-std=c++11 -O3 -DNDEBUG -D_GLIBCXX_USE_CXX11_ABI=0 -fPIC")

function(add_third_party name)
  CMAKE_PARSE_ARGUMENTS(parsed "NOPARALLEL" "CMAKE_PASS_FLAGS" "" ${ARGN})
  set(BUILD_OPTIONS "-j4")
  if (parsed_NOPARALLEL)
    set(BUILD_OPTIONS "")
  endif()
  if (parsed_CMAKE_PASS_FLAGS)
    string(REPLACE " " ";" list_CMAKE_ARGS ${parsed_CMAKE_PASS_FLAGS})
  endif()

  ExternalProject_Add(${name}_project
    DOWNLOAD_DIR ${THIRD_PARTY_DIR}/${name}
    SOURCE_DIR ${THIRD_PARTY_DIR}/${name}
    UPDATE_COMMAND ""
    BUILD_COMMAND $(MAKE) ${BUILD_OPTIONS}
    # Wrap download, configure and build steps in a script to log output
    LOG_INSTALL ON
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON

    # we need those CMAKE_ARGS for cmake based 3rd party projects.
    CMAKE_ARGS -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY:PATH=${THIRD_PARTY_LIB_DIR}/${name}
        -DCMAKE_LIBRARY_OUTPUT_DIRECTORY:PATH=${THIRD_PARTY_LIB_DIR}/${name}
        -DCMAKE_BUILD_TYPE:STRING=Release
        -DCMAKE_C_FLAGS:STRING=-O3 -DCMAKE_CXX_FLAGS=${THIRD_PARTY_CXX_FLAGS}
        -DCMAKE_INSTALL_PREFIX:PATH=${THIRD_PARTY_LIB_DIR}/${name}
        ${list_CMAKE_ARGS}
    ${parsed_UNPARSED_ARGUMENTS}
    )

  string(TOUPPER ${name} uname)
  set("${uname}_INCLUDE_DIR" "${THIRD_PARTY_LIB_DIR}/${name}/include" PARENT_SCOPE)
  set("${uname}_LIB_DIR" "${THIRD_PARTY_LIB_DIR}/${name}/lib" PARENT_SCOPE)

endfunction()

function(declare_imported_lib name path)
  add_library(${name} STATIC IMPORTED)
  set_property(TARGET ${name} PROPERTY IMPORTED_LOCATION ${path}/lib${name}.a)
  add_dependencies(${name} ${ARGN})
endfunction()

function(declare_shared_lib name path)
  add_library(${name} SHARED IMPORTED)
  set_property(TARGET ${name} PROPERTY IMPORTED_LOCATION ${path}/lib${name}.so)
  add_dependencies(${name} ${ARGN})
endfunction()

# We need gflags as shared library because glog is shared and it uses it too.
# Gflags can not be duplicated inside executable due to its module initialization logic.
add_third_party(
    gflags
    GIT_REPOSITORY https://github.com/gflags/gflags.git
    GIT_TAG "v2.1.2"
    CMAKE_PASS_FLAGS "-DBUILD_PACKAGING=OFF -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_STATIC_LIBS=OFF -DBUILD_gflags_nothreads_LIB=OFF"
)

add_third_party(
    gtest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 13206d6
)

add_third_party(
    gperf
    GIT_REPOSITORY https://github.com/romange/gperftools.git
    PATCH_COMMAND ./autogen.sh
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --enable-frame-pointers --prefix=${THIRD_PARTY_LIB_DIR}/gperf
)

add_third_party(
  glog

  DEPENDS gflags_project
  GIT_REPOSITORY https://github.com/UbimoLTD/glog.git
  PATCH_COMMAND autoreconf --force --install  # needed to refresh toolchain
  CONFIGURE_COMMAND <SOURCE_DIR>/configure
       #--disable-rtti we can not use rtti because of the fucking thrift.
       --with-gflags=${THIRD_PARTY_LIB_DIR}/gflags
       --enable-frame-pointers
       --prefix=${THIRD_PARTY_LIB_DIR}/glog
       --enable-static=no
       CXXFLAGS=${THIRD_PARTY_CXX_FLAGS}
  INSTALL_COMMAND make install
  COMMAND sed -i "s/GOOGLE_PREDICT_/GOOGLE_GLOG_PREDICT_/g" ${THIRD_PARTY_LIB_DIR}/glog/include/glog/logging.h
)

set(SNAPPY_DIR "${THIRD_PARTY_LIB_DIR}/snappy")
add_third_party(snappy GIT_REPOSITORY https://github.com/google/snappy.git
  PATCH_COMMAND ./autogen.sh
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${THIRD_PARTY_LIB_DIR}/snappy
      CXXFLAGS=${THIRD_PARTY_CXX_FLAGS}
)

add_third_party(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG f662e8be5bc9d40640e10b72092780b401612bf2
)

set(XXHASH_DIR ${THIRD_PARTY_LIB_DIR}/xxhash)
add_third_party(
  xxhash
  GIT_REPOSITORY https://github.com/UbimoLTD/xxHash.git
  # GIT_TAG r42
  CONFIGURE_COMMAND cmake -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX=${XXHASH_DIR} -DBUILD_STATIC_LIBS=ON <SOURCE_DIR>/cmake_unofficial/
)

set(CITYHASH_DIR ${THIRD_PARTY_LIB_DIR}/cityhash)
add_third_party(cityhash
  GIT_REPOSITORY https://github.com/google/cityhash.git
  PATCH_COMMAND autoreconf --force --install
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${CITYHASH_DIR} --enable-sse4.2 CXXFLAGS=${THIRD_PARTY_CXX_FLAGS}
)

set(SPARSEHASH_DIR ${THIRD_PARTY_LIB_DIR}/sparsehash)
add_third_party(
  sparsehash
  GIT_REPOSITORY https://github.com/romange/sparsehash.git
  CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${SPARSEHASH_DIR} CXXFLAGS=${THIRD_PARTY_CXX_FLAGS}
)
set(SPARSEHASH_INCLUDE_DIR ${SPARSEHASH_DIR}/include)

#Protobuf project
set(PROTOBUF_DIR ${THIRD_PARTY_LIB_DIR}/protobuf)
set(PROTOC ${PROTOBUF_DIR}/bin/protoc)

add_third_party(
    protobuf
    GIT_REPOSITORY https://github.com/romange/protobuf.git
    GIT_TAG roman_cxx11_move-3.0.0-beta-2
    PATCH_COMMAND ./autogen.sh

    CONFIGURE_COMMAND <SOURCE_DIR>/configure --with-zlib  --with-tests=no
        CXXFLAGS=${THIRD_PARTY_CXX_FLAGS} --prefix=${PROTOBUF_DIR}
    COMMAND make clean
    BUILD_IN_SOURCE 1
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/proto_python_setup.cmd"
     "PROTOC=${PROTOC} python setup.py build\n"
     "PYTHONPATH=${THIRD_PARTY_LIB_DIR}/lib/python/ python setup.py install "
     "--home=${THIRD_PARTY_LIB_DIR}\n"
     "cd `mktemp -d`\n"
     "mkdir google\n"
     "echo > google/__init__.py\n"
     "zip ${THIRD_PARTY_LIB_DIR}/lib/python/protobuf-3.0.0b2-py2.7.egg google/__init__.py"
     )

ExternalProject_Add_Step(protobuf_project install_python
  DEPENDEES install
  WORKING_DIRECTORY ${THIRD_PARTY_DIR}/protobuf/python
  COMMAND mkdir -p ${THIRD_PARTY_LIB_DIR}/lib/python
  COMMAND bash ${CMAKE_CURRENT_BINARY_DIR}/proto_python_setup.cmd
  LOG 1
)

add_library(fast_malloc SHARED IMPORTED)
set_property(TARGET fast_malloc PROPERTY IMPORTED_LOCATION
             ${THIRD_PARTY_LIB_DIR}/gperf/lib/libtcmalloc_and_profiler.so)

add_library(gtest STATIC IMPORTED)
set_property(TARGET gtest PROPERTY IMPORTED_LOCATION ${GTEST_LIB_DIR}/libgmock.a)
add_dependencies(gtest gtest_project)


declare_imported_lib(benchmark ${BENCHMARK_LIB_DIR} benchmark_project)
declare_shared_lib(cityhash ${CITYHASH_LIB_DIR} cityhash_project)
declare_shared_lib(glog ${GLOG_LIB_DIR} glog_project)
declare_shared_lib(protobuf ${PROTOBUF_LIB_DIR} protobuf_project)
declare_shared_lib(snappy ${SNAPPY_LIB_DIR} snappy_project)
declare_imported_lib(xxhash ${XXHASH_LIB_DIR} xxhash_project)

file(MAKE_DIRECTORY ${BENCHMARK_INCLUDE_DIR})
file(MAKE_DIRECTORY ${CITYHASH_INCLUDE_DIR})
file(MAKE_DIRECTORY ${GLOG_INCLUDE_DIR})
file(MAKE_DIRECTORY ${GTEST_INCLUDE_DIR})
file(MAKE_DIRECTORY ${PROTOBUF_INCLUDE_DIR})
file(MAKE_DIRECTORY ${SNAPPY_INCLUDE_DIR})
file(MAKE_DIRECTORY ${SPARSEHASH_INCLUDE_DIR})
file(MAKE_DIRECTORY ${XXHASH_INCLUDE_DIR})

set_property(TARGET benchmark PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${BENCHMARK_INCLUDE_DIR})
set_property(TARGET cityhash PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CITYHASH_INCLUDE_DIR})
set_property(TARGET glog PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GLOG_INCLUDE_DIR})
set_property(TARGET gtest PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${GTEST_INCLUDE_DIR})
set_property(TARGET protobuf PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${PROTOBUF_INCLUDE_DIR})
set_property(TARGET snappy PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${SNAPPY_INCLUDE_DIR})
set_property(TARGET xxhash PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${XXHASH_INCLUDE_DIR})
set_target_properties(gtest PROPERTIES IMPORTED_LINK_INTERFACE_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

include_directories(${GPERF_INCLUDE_DIR})
include_directories(${PROTOBUF_INCLUDE_DIR})
include_directories(${SPARSEHASH_INCLUDE_DIR})

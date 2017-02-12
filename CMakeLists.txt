cmake_minimum_required(VERSION 2.8.10)

project(ubimo_list_file CXX C)

set(PROJECT_CONTACT ori@ubimo.com)

enable_testing()

include_directories("${PROJECT_SOURCE_DIR}")

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

include(internal.cmake)
include(third_party.cmake)

add_subdirectory(base)
add_subdirectory(demo)
add_subdirectory(file)
add_subdirectory(strings)
add_subdirectory(util)
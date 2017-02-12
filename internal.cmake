set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(ROOT_GEN_DIR ${CMAKE_SOURCE_DIR}/genfiles)
file(MAKE_DIRECTORY ${ROOT_GEN_DIR})
include_directories(${ROOT_GEN_DIR})

macro(add_compile_flag target)
  set_property(TARGET ${target} APPEND PROPERTY COMPILE_FLAGS ${ARGN})
endmacro()

macro(add_include target)
  set_property(TARGET ${target}
               APPEND PROPERTY INCLUDE_DIRECTORIES ${ARGN})
endmacro()

function(cur_gen_dir out_dir)
  file(RELATIVE_PATH _rel_folder "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  set(_tmp_dir ${ROOT_GEN_DIR}/${_rel_folder})
  set(${out_dir} ${_tmp_dir} PARENT_SCOPE)
  file(MAKE_DIRECTORY ${_tmp_dir})
endfunction()

function(gen_cxx_files list_name extension dirname)
  set(_tmp_l "")
  foreach (_file ${ARGN})
    LIST(APPEND _tmp_l "${dirname}/${_file}.h" "${dirname}/${_file}.${extension}")
  endforeach(_file)
  set(${list_name} ${_tmp_l} PARENT_SCOPE)
endfunction()

function(cxx_proto_lib name)
  cur_gen_dir(gen_dir)

  # create __init__.py in all parent directories that lead to protobuf to support
  # python proto files.
  file(RELATIVE_PATH gen_rel_path ${ROOT_GEN_DIR} ${gen_dir})
  string(REPLACE "/" ";" parent_list ${gen_rel_path})
  set(cur_parent ${ROOT_GEN_DIR})
  foreach (rel_parent ${parent_list})
    set(cur_parent "${cur_parent}/${rel_parent}")
    file(WRITE "${cur_parent}/__init__.py" "")
  endforeach(rel_parent)

  gen_cxx_files(cxx_out_files "cc" ${gen_dir} ${name}.pb)

  GET_FILENAME_COMPONENT(absolute_proto_name ${name}.proto ABSOLUTE)

  CMAKE_PARSE_ARGUMENTS(parsed "PY" "" "DEPENDS" ${ARGN})
  set(prefix_command ${PROTOC} ${absolute_proto_name} --proto_path=${CMAKE_SOURCE_DIR})
  set(py_command cat /dev/null)
  if (parsed_PY)
    set(py_command ${prefix_command} --proto_path=${PROTOBUF_INCLUDE_DIR} --python_out=${ROOT_GEN_DIR})
  endif()
  set(lib_link_dep "")
  set(plugins_arg "")
  set(proj_depends "protobuf_project")
  ADD_CUSTOM_COMMAND(
           OUTPUT ${cxx_out_files}
           COMMAND ${PROTOC} ${absolute_proto_name}
                   --proto_path=${CMAKE_SOURCE_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR}
                   --cpp_out=${ROOT_GEN_DIR} --cpp_opt=cxx11  ${plugins_arg}
           COMMAND ${py_command}
           COMMAND touch ${gen_dir}/__init__.py
           DEPENDS ${name}.proto DEPENDS ${proj_depends} ${parsed_DEPENDS}
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
           COMMENT "Generating sources from ${absolute_proto_name}" VERBATIM)
  set_source_files_properties(${cxx_out_files}
                              PROPERTIES GENERATED TRUE)
  set(lib_name "${name}_proto")
  add_library(${lib_name} ${cxx_out_files})
  target_link_libraries(${lib_name} ${parsed_DEPENDS} protobuf ${lib_link_dep})
  add_include(${lib_name} ${PROTOBUF_INCLUDE_DIR})
  add_compile_flag(${lib_name} "-DGOOGLE_PROTOBUF_NO_RTTI -Wno-unused-parameter")
endfunction()

function(flex_lib name)
  GET_FILENAME_COMPONENT(_in ${name}.lex ABSOLUTE)
  cur_gen_dir(gen_dir)
  set(lib_name "${name}_flex")

  set(full_path_cc ${gen_dir}/${name}.cc)
  ADD_CUSTOM_COMMAND(
           OUTPUT ${full_path_cc}
           COMMAND mkdir -p ${gen_dir}
           COMMAND ${CMAKE_COMMAND} -E remove ${gen_dir}/${name}.ih
           COMMAND flex -o ${gen_dir}/${name}.cc ${_in}
           DEPENDS ${_in}
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
           COMMENT "Generating lexer from ${name}.lex" VERBATIM)

  set_source_files_properties(${gen_dir}/${name}.h ${gen_dir}/${name}.cc ${gen_dir}/${name}_base.h
                              PROPERTIES GENERATED TRUE)

  # ORI: plang_parser.hh is here because this must be generated after the parser is generated
  add_library(${lib_name} ${gen_dir}/${name}.cc ${gen_dir}/plang_parser.hh)
  add_compile_flag(${lib_name} "-Wno-extra")
  target_link_libraries(${lib_name} glog)
endfunction()

function(bison_lib name)
  GET_FILENAME_COMPONENT(_in ${name}.y ABSOLUTE)
  cur_gen_dir(gen_dir)
  set(lib_name "${name}_bison")
  add_library(${lib_name} ${gen_dir}/${name}.cc)
  add_compile_flag(${lib_name} "-frtti")
  set(full_path_cc ${gen_dir}/${name}.cc ${gen_dir}/${name}.hh)
  ADD_CUSTOM_COMMAND(
           OUTPUT ${full_path_cc}
           COMMAND mkdir -p ${gen_dir}
           COMMAND bison --language=c++ -o ${gen_dir}/${name}.cc ${name}.y
           DEPENDS ${_in}
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
           COMMENT "Generating parser from ${name}.y" VERBATIM)
 set_source_files_properties(${name}.cc ${name}_base.h PROPERTIES GENERATED TRUE)
endfunction()

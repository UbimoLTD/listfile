add_library(strings ascii_ctype.cc charset.cc escaping.cc join.cc memutil.cc numbers.cc split.cc
                    strcat.cc stringpiece.cc stringprintf.cc strip.cc strtoint.cc util.cc utf8/rune.c)
add_dependencies(strings sparsehash_project)
target_link_libraries(strings glog gflags)

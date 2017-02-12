add_library(math mathlimits.cc mathutil.cc exactfloat/exactfloat.cc)
target_link_libraries(math base crypto)
add_library(sstable block.cc block_builder.cc filter_block.cc format.cc iterator.cc sstable.cc
            sorting_builder.cc sstable_builder.cc two_level_iterator.cc)
target_link_libraries(sstable file snappy base strings util)

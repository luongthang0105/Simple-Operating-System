cmake_minimum_required(VERSION 3.7.2)

project(libsync C)

add_library(sync EXCLUDE_FROM_ALL src/mutex.c)
target_include_directories(sync INTERFACE include)
target_link_libraries(sync muslc sel4 aos utils)

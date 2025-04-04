cmake_minimum_required(VERSION 3.0)
project(net)
aux_source_directory(./ SRC)
include_directories(${HEAD_PATH})
set(LIBRARY_OUTPUT_PATH ${LIB_PATH})
add_library(${NET_LIB} STATIC ${SRC})


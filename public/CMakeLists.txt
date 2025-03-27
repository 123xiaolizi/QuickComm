cmake_minimum_required(VERSION 3.0)
project(public)
aux_source_directory(./ SRC)
include_directories(${HEAD_PATH})
set(LIBRARY_OUTPUT_PATH ${LIB_PATH})
add_library(${PUBLIC_LIB} STATIC ${SRC})


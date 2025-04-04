cmake_minimum_required(VERSION 3.0)
project(quickcomm)
set(CMAKE_CXX_STANDARD 11)
#静态库生成路径
set(LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib)
#可执行程序生成路径
set(EXEC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
#头文件路径
set(HEAD_PATH ${CMAKE_CURRENT_SOURCE_DIR}/include)
#静态库的名字
set(NET_LIB net)
set(PROC_LIB proc)
set(PUBLIC_LIB public)
set(SIGNAL_LIB signal)
set(MISC_LIB signal)

#可执行程序名字
set(APP_NAME QuickComm)

#添加子目录
add_subdirectory(net)
add_subdirectory(misc)
add_subdirectory(proc)
add_subdirectory(public)
#add_subdirectory(signal)
add_subdirectory(app)
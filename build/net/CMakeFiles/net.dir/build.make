# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/work/QuickComm

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/work/QuickComm/build

# Include any dependencies generated for this target.
include net/CMakeFiles/net.dir/depend.make

# Include the progress variables for this target.
include net/CMakeFiles/net.dir/progress.make

# Include the compile flags for this target's objects.
include net/CMakeFiles/net.dir/flags.make

net/CMakeFiles/net.dir/qc_socket_accept.cc.o: net/CMakeFiles/net.dir/flags.make
net/CMakeFiles/net.dir/qc_socket_accept.cc.o: ../net/qc_socket_accept.cc
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/work/QuickComm/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object net/CMakeFiles/net.dir/qc_socket_accept.cc.o"
	cd /home/work/QuickComm/build/net && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/net.dir/qc_socket_accept.cc.o -c /home/work/QuickComm/net/qc_socket_accept.cc

net/CMakeFiles/net.dir/qc_socket_accept.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/net.dir/qc_socket_accept.cc.i"
	cd /home/work/QuickComm/build/net && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/work/QuickComm/net/qc_socket_accept.cc > CMakeFiles/net.dir/qc_socket_accept.cc.i

net/CMakeFiles/net.dir/qc_socket_accept.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/net.dir/qc_socket_accept.cc.s"
	cd /home/work/QuickComm/build/net && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/work/QuickComm/net/qc_socket_accept.cc -o CMakeFiles/net.dir/qc_socket_accept.cc.s

net/CMakeFiles/net.dir/qc_socket_conn.cc.o: net/CMakeFiles/net.dir/flags.make
net/CMakeFiles/net.dir/qc_socket_conn.cc.o: ../net/qc_socket_conn.cc
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/work/QuickComm/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object net/CMakeFiles/net.dir/qc_socket_conn.cc.o"
	cd /home/work/QuickComm/build/net && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/net.dir/qc_socket_conn.cc.o -c /home/work/QuickComm/net/qc_socket_conn.cc

net/CMakeFiles/net.dir/qc_socket_conn.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/net.dir/qc_socket_conn.cc.i"
	cd /home/work/QuickComm/build/net && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/work/QuickComm/net/qc_socket_conn.cc > CMakeFiles/net.dir/qc_socket_conn.cc.i

net/CMakeFiles/net.dir/qc_socket_conn.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/net.dir/qc_socket_conn.cc.s"
	cd /home/work/QuickComm/build/net && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/work/QuickComm/net/qc_socket_conn.cc -o CMakeFiles/net.dir/qc_socket_conn.cc.s

# Object files for target net
net_OBJECTS = \
"CMakeFiles/net.dir/qc_socket_accept.cc.o" \
"CMakeFiles/net.dir/qc_socket_conn.cc.o"

# External object files for target net
net_EXTERNAL_OBJECTS =

../lib/libnet.a: net/CMakeFiles/net.dir/qc_socket_accept.cc.o
../lib/libnet.a: net/CMakeFiles/net.dir/qc_socket_conn.cc.o
../lib/libnet.a: net/CMakeFiles/net.dir/build.make
../lib/libnet.a: net/CMakeFiles/net.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/work/QuickComm/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Linking CXX static library ../../lib/libnet.a"
	cd /home/work/QuickComm/build/net && $(CMAKE_COMMAND) -P CMakeFiles/net.dir/cmake_clean_target.cmake
	cd /home/work/QuickComm/build/net && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/net.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
net/CMakeFiles/net.dir/build: ../lib/libnet.a

.PHONY : net/CMakeFiles/net.dir/build

net/CMakeFiles/net.dir/clean:
	cd /home/work/QuickComm/build/net && $(CMAKE_COMMAND) -P CMakeFiles/net.dir/cmake_clean.cmake
.PHONY : net/CMakeFiles/net.dir/clean

net/CMakeFiles/net.dir/depend:
	cd /home/work/QuickComm/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/work/QuickComm /home/work/QuickComm/net /home/work/QuickComm/build /home/work/QuickComm/build/net /home/work/QuickComm/build/net/CMakeFiles/net.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : net/CMakeFiles/net.dir/depend


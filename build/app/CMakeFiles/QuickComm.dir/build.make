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
include app/CMakeFiles/QuickComm.dir/depend.make

# Include the progress variables for this target.
include app/CMakeFiles/QuickComm.dir/progress.make

# Include the compile flags for this target's objects.
include app/CMakeFiles/QuickComm.dir/flags.make

app/CMakeFiles/QuickComm.dir/QuickComm.cc.o: app/CMakeFiles/QuickComm.dir/flags.make
app/CMakeFiles/QuickComm.dir/QuickComm.cc.o: ../app/QuickComm.cc
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/work/QuickComm/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object app/CMakeFiles/QuickComm.dir/QuickComm.cc.o"
	cd /home/work/QuickComm/build/app && /usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/QuickComm.dir/QuickComm.cc.o -c /home/work/QuickComm/app/QuickComm.cc

app/CMakeFiles/QuickComm.dir/QuickComm.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/QuickComm.dir/QuickComm.cc.i"
	cd /home/work/QuickComm/build/app && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/work/QuickComm/app/QuickComm.cc > CMakeFiles/QuickComm.dir/QuickComm.cc.i

app/CMakeFiles/QuickComm.dir/QuickComm.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/QuickComm.dir/QuickComm.cc.s"
	cd /home/work/QuickComm/build/app && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/work/QuickComm/app/QuickComm.cc -o CMakeFiles/QuickComm.dir/QuickComm.cc.s

# Object files for target QuickComm
QuickComm_OBJECTS = \
"CMakeFiles/QuickComm.dir/QuickComm.cc.o"

# External object files for target QuickComm
QuickComm_EXTERNAL_OBJECTS =

../bin/QuickComm: app/CMakeFiles/QuickComm.dir/QuickComm.cc.o
../bin/QuickComm: app/CMakeFiles/QuickComm.dir/build.make
../bin/QuickComm: ../lib/libpublic.a
../bin/QuickComm: app/CMakeFiles/QuickComm.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/work/QuickComm/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../../bin/QuickComm"
	cd /home/work/QuickComm/build/app && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/QuickComm.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
app/CMakeFiles/QuickComm.dir/build: ../bin/QuickComm

.PHONY : app/CMakeFiles/QuickComm.dir/build

app/CMakeFiles/QuickComm.dir/clean:
	cd /home/work/QuickComm/build/app && $(CMAKE_COMMAND) -P CMakeFiles/QuickComm.dir/cmake_clean.cmake
.PHONY : app/CMakeFiles/QuickComm.dir/clean

app/CMakeFiles/QuickComm.dir/depend:
	cd /home/work/QuickComm/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/work/QuickComm /home/work/QuickComm/app /home/work/QuickComm/build /home/work/QuickComm/build/app /home/work/QuickComm/build/app/CMakeFiles/QuickComm.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : app/CMakeFiles/QuickComm.dir/depend


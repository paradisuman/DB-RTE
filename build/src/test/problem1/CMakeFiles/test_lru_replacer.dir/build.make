# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
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
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/pa/db/db2023-runtimeterror

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/pa/db/db2023-runtimeterror/build

# Include any dependencies generated for this target.
include src/test/problem1/CMakeFiles/test_lru_replacer.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include src/test/problem1/CMakeFiles/test_lru_replacer.dir/compiler_depend.make

# Include the progress variables for this target.
include src/test/problem1/CMakeFiles/test_lru_replacer.dir/progress.make

# Include the compile flags for this target's objects.
include src/test/problem1/CMakeFiles/test_lru_replacer.dir/flags.make

src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o: src/test/problem1/CMakeFiles/test_lru_replacer.dir/flags.make
src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o: ../src/test/problem1/test_lru_replacer.cpp
src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o: src/test/problem1/CMakeFiles/test_lru_replacer.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/pa/db/db2023-runtimeterror/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o"
	cd /home/pa/db/db2023-runtimeterror/build/src/test/problem1 && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o -MF CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o.d -o CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o -c /home/pa/db/db2023-runtimeterror/src/test/problem1/test_lru_replacer.cpp

src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.i"
	cd /home/pa/db/db2023-runtimeterror/build/src/test/problem1 && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/pa/db/db2023-runtimeterror/src/test/problem1/test_lru_replacer.cpp > CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.i

src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.s"
	cd /home/pa/db/db2023-runtimeterror/build/src/test/problem1 && /usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/pa/db/db2023-runtimeterror/src/test/problem1/test_lru_replacer.cpp -o CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.s

# Object files for target test_lru_replacer
test_lru_replacer_OBJECTS = \
"CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o"

# External object files for target test_lru_replacer
test_lru_replacer_EXTERNAL_OBJECTS =

bin/test_lru_replacer: src/test/problem1/CMakeFiles/test_lru_replacer.dir/test_lru_replacer.cpp.o
bin/test_lru_replacer: src/test/problem1/CMakeFiles/test_lru_replacer.dir/build.make
bin/test_lru_replacer: lib/libgtest_main.a
bin/test_lru_replacer: lib/liblru_replacer.a
bin/test_lru_replacer: lib/libgtest.a
bin/test_lru_replacer: src/test/problem1/CMakeFiles/test_lru_replacer.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/pa/db/db2023-runtimeterror/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../../../bin/test_lru_replacer"
	cd /home/pa/db/db2023-runtimeterror/build/src/test/problem1 && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_lru_replacer.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
src/test/problem1/CMakeFiles/test_lru_replacer.dir/build: bin/test_lru_replacer
.PHONY : src/test/problem1/CMakeFiles/test_lru_replacer.dir/build

src/test/problem1/CMakeFiles/test_lru_replacer.dir/clean:
	cd /home/pa/db/db2023-runtimeterror/build/src/test/problem1 && $(CMAKE_COMMAND) -P CMakeFiles/test_lru_replacer.dir/cmake_clean.cmake
.PHONY : src/test/problem1/CMakeFiles/test_lru_replacer.dir/clean

src/test/problem1/CMakeFiles/test_lru_replacer.dir/depend:
	cd /home/pa/db/db2023-runtimeterror/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/pa/db/db2023-runtimeterror /home/pa/db/db2023-runtimeterror/src/test/problem1 /home/pa/db/db2023-runtimeterror/build /home/pa/db/db2023-runtimeterror/build/src/test/problem1 /home/pa/db/db2023-runtimeterror/build/src/test/problem1/CMakeFiles/test_lru_replacer.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : src/test/problem1/CMakeFiles/test_lru_replacer.dir/depend


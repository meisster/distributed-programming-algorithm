# CMAKE generated file: DO NOT EDIT!
# Generated by "NMake Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE
NULL=nul
!ENDIF
SHELL = cmd.exe

# The CMake executable.
CMAKE_COMMAND = C:\Users\Meisster\AppData\Local\JetBrains\Toolbox\apps\CLion\ch-0\201.7846.88\bin\cmake\win\bin\cmake.exe

# The command to remove a file.
RM = C:\Users\Meisster\AppData\Local\JetBrains\Toolbox\apps\CLion\ch-0\201.7846.88\bin\cmake\win\bin\cmake.exe -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = C:\Users\Meisster\Desktop\szafa

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = C:\Users\Meisster\Desktop\szafa\cmake-build-debug

# Include any dependencies generated for this target.
include CMakeFiles\szafa.dir\depend.make

# Include the progress variables for this target.
include CMakeFiles\szafa.dir\progress.make

# Include the compile flags for this target's objects.
include CMakeFiles\szafa.dir\flags.make

CMakeFiles\szafa.dir\main.cpp.obj: CMakeFiles\szafa.dir\flags.make
CMakeFiles\szafa.dir\main.cpp.obj: ..\main.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=C:\Users\Meisster\Desktop\szafa\cmake-build-debug\CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/szafa.dir/main.cpp.obj"
	C:\PROGRA~2\MICROS~2\2017\BUILDT~1\VC\Tools\MSVC\1416~1.270\bin\Hostx86\x86\cl.exe @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) /FoCMakeFiles\szafa.dir\main.cpp.obj /FdCMakeFiles\szafa.dir\ /FS -c C:\Users\Meisster\Desktop\szafa\main.cpp
<<

CMakeFiles\szafa.dir\main.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/szafa.dir/main.cpp.i"
	C:\PROGRA~2\MICROS~2\2017\BUILDT~1\VC\Tools\MSVC\1416~1.270\bin\Hostx86\x86\cl.exe > CMakeFiles\szafa.dir\main.cpp.i @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E C:\Users\Meisster\Desktop\szafa\main.cpp
<<

CMakeFiles\szafa.dir\main.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/szafa.dir/main.cpp.s"
	C:\PROGRA~2\MICROS~2\2017\BUILDT~1\VC\Tools\MSVC\1416~1.270\bin\Hostx86\x86\cl.exe @<<
 /nologo /TP $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) /FoNUL /FAs /FaCMakeFiles\szafa.dir\main.cpp.s /c C:\Users\Meisster\Desktop\szafa\main.cpp
<<

# Object files for target szafa
szafa_OBJECTS = \
"CMakeFiles\szafa.dir\main.cpp.obj"

# External object files for target szafa
szafa_EXTERNAL_OBJECTS =

szafa.exe: CMakeFiles\szafa.dir\main.cpp.obj
szafa.exe: CMakeFiles\szafa.dir\build.make
szafa.exe: CMakeFiles\szafa.dir\objects1.rsp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=C:\Users\Meisster\Desktop\szafa\cmake-build-debug\CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable szafa.exe"
	C:\Users\Meisster\AppData\Local\JetBrains\Toolbox\apps\CLion\ch-0\201.7846.88\bin\cmake\win\bin\cmake.exe -E vs_link_exe --intdir=CMakeFiles\szafa.dir --rc=C:\PROGRA~2\WI3CF2~1\10\bin\100177~1.0\x86\rc.exe --mt=C:\PROGRA~2\WI3CF2~1\10\bin\100177~1.0\x86\mt.exe --manifests  -- C:\PROGRA~2\MICROS~2\2017\BUILDT~1\VC\Tools\MSVC\1416~1.270\bin\Hostx86\x86\link.exe /nologo @CMakeFiles\szafa.dir\objects1.rsp @<<
 /out:szafa.exe /implib:szafa.lib /pdb:C:\Users\Meisster\Desktop\szafa\cmake-build-debug\szafa.pdb /version:0.0  /machine:X86 /debug /INCREMENTAL /subsystem:console  kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib 
<<

# Rule to build all files generated by this target.
CMakeFiles\szafa.dir\build: szafa.exe

.PHONY : CMakeFiles\szafa.dir\build

CMakeFiles\szafa.dir\clean:
	$(CMAKE_COMMAND) -P CMakeFiles\szafa.dir\cmake_clean.cmake
.PHONY : CMakeFiles\szafa.dir\clean

CMakeFiles\szafa.dir\depend:
	$(CMAKE_COMMAND) -E cmake_depends "NMake Makefiles" C:\Users\Meisster\Desktop\szafa C:\Users\Meisster\Desktop\szafa C:\Users\Meisster\Desktop\szafa\cmake-build-debug C:\Users\Meisster\Desktop\szafa\cmake-build-debug C:\Users\Meisster\Desktop\szafa\cmake-build-debug\CMakeFiles\szafa.dir\DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles\szafa.dir\depend

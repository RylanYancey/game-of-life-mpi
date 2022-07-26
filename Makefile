## HOW TO USE THIS MAKEFILE ##

## WHAT THIS MAKEFILE IS FOR
# - Building or 'making' MPI C++ projects using MpiCC

## FILE STRUCTURE
# This makefile will look for a HEADER file, in which all of
# your header (.h) files should be placed.
# It will also look for a SOURCE file, in which all of
# your source (.cpp) files should be placed. 
# It will put all of its compiled Object files into a BIN file. 
# Note: header files must use .h extension. 
# Note: header, source, and bin folder should be lowercase.
# Note: if using VSCODE, a c_cpp_configurations folder is also required. 

## BUILD A PROJECT
# Type 'make' in the terminal while in the same directory as the makefile. 
# That will instruct this makefile to run.
# After using 'make', you should see a 'run' binary appear in the same
# directory as this makefile. 
# You may also note the 'bin' file will have .o files in it. (object files)
# These object files are then compiled together to make the binary, run. 
# to run the binary, use './run' in the terminal. 
#
# Note: by default, using 'make' with this file will not run it. However, if you
# use 'make np=4', it will launch MPI with 4 cores. 

## CLEAN THE BUILD
# Type 'make clean' in the terminal. 
# This will remove all .o files from bin, allowing you
# to have a clean make. This is necessary when switching compiler versions, 
# switching OSs', or any other operation that would use a different compiler. 

## LINK A LIBRARY
# To link a library, change the 'LIBRARIES = ' variable to include whatever you want.
# For example, to include the linear algebra library aramdillo, simply put:
# LIBRARIES = -larmadillo
# Note: Library must be installed first. 
# Note: For vscode intellisense to work, you may have to have microsoft's
# makefile_tools extension installed. 

## USER-MODIFYABLE VARIABLES ##

# Add libraries to link here. 
LIBRARIES = 

# The name of the binary file that will be produced. 
NAME_OF_BIN = run

## USER - MODIFYABLE VARIABLES ##

### ACTUAL MAKEFILE ### USER STAY OUT ###

# CC     = The compiler we want to use. In this case, MPI C++ Compiler.
# CFLAGS = Flags for the compiler. These flags tell it to compile a .cpp file (-c)
# into a .o file, and to look for header and source files in the ./header and ./source
# folders, states by using the -I flag.  
CC = mpiCC
CFLAGS = -c -I./header

# Collects all of the source files into a single object. 
# src = all .cpp in source
# src1 = changes .cpp to .o in src.  
src = $(wildcard source/*.cpp)
src1 = $(src:.cpp=.o)

# changes the name of every object in src1 to be
# in the format bin/NAME.o, instead of source/NAME.o.
# It must be in this format, since all of our .o files
# will be in the bin folder. 
objects := $(src1:source/%=bin/%)

# The 'all' recipe, that will produce the binary to run.
# $(objects) is all of our targets, which is all of the .o files in bin.
# you can read this as an example:
#
# mpiCC -o run bin/main.o -larmadillo
#
# if we only had a main.cpp in source, and were including the armadillo library. 
all : $(objects)
	$(CC) -o $(NAME_OF_BIN) $(objects) $(LIBRARIES)

# Recipe for for all bin/.o files, where the prerequisite is the correpsonding source file.
# Heres an example for what this would read like if there was only a main.cpp file:
#
# bin/main.o : source/main.cu
# 	   nvcc -c -I./header -I./source $?=ALL_PREREQUISITES
#      move the generated .o file to bin.
#
# %, is short hand in GNU make for 'ALL'. Kind of like bin/*.o. 
# $? is short hand for all prerequisites (#includes) of a file. 
bin/%.o : source/%.cpp
	$(CC) $(CFLAGS) $?
	mv *.o bin

# Recipe to remove all bin/*.o files.
.PHONY : clean
clean : 
	rm -rf all $(objects)
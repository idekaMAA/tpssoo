# Libraries
LIBS=utils commons pthread readline m crypto

# Custom libraries' paths
SHARED_LIBPATHS=
STATIC_LIBPATHS=../utils

#include paths
INCLUDE_PATHS=-I../src

# Compiler flags
CDEBUG=-g -Wall -DDEBUG -fdiagnostics-color=always $(INCLUDE_PATHS)
CRELEASE=-O3 -Wall -DNDEBUG $(INCLUDE_PATHS)

# Source files (*.c) to be excluded from tests compilation
TEST_EXCLUDE=src/main.c

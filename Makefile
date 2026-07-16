# Makefile for c64asm, built from its per-module source files.
#
#   make			builds ./c64asm
#   make clean		removes build artifacts
#	make tests		build test prg files
#	make examples	build example prg files
#
# Works with gcc or clang; on macOS the Xcode command-line tools provide
# a `cc` that behaves the same way, so this Makefile needs no changes
# there.

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra

SRC_DIR := src
BIN_DIR := bin
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := $(BIN_DIR)/c64asm


TST_DIR := tests
TST_SRC := $(wildcard $(TST_DIR)/*.asm)
TST_PRG := $(TST_SRC:.asm=.prg)

EXA_DIR := examples
EXA_SRC := $(wildcard $(EXA_DIR)/*.asm)
EXA_PRG := $(EXA_SRC:.asm=.prg)

# Or use patsubst for more control
# OBJS := $(patsubst %.c, %.o, $(SRCS))

all: $(OBJS)

%.o: %.c
	@echo "Compiling $< to $@"


.PHONY: all clean

all: $(TARGET)

# Compile C
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cleanup
clean:
	rm -f $(TARGET) $(SRC_DIR)/*.o src_1/c64asm
	rm -f $(TST_PRG) $(TST_DIR)/*.lst $(TST_DIR)/vice.log
	rm -f $(EXA_PRG) $(EXA_DIR)/*.lst $(EXA_DIR)/vice.log 

# Assembly files to PRG

tests: $(TARGET) $(TST_PRG)

examples: $(TARGET) $(EXA_PRG)


%.prg: %.asm
	echo $(TARGET) $< -o $@
	$(TARGET) $< -o $@ --listing $(<:.asm=.lst)



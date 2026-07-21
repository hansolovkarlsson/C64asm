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

# Assembler
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra

# Multi-file
SRC_DIR := src
BIN_DIR := bin
SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := $(BIN_DIR)/c64asm

# Single-file
SINGLE_DIR := single_src
SINGLE_TARGET := $(SINGLE_DIR)/c64asm
SINGLE_SOURCE := $(SINGLE_DIR)/c64asm.c

# C64 Programs
# CASM = [ bin/c64asm single_src/c64asm "python3 single_src/c64asm.py"]
CASM    ?= $(TARGET)
EXA_DIR := examples
EXA_SRC := $(wildcard $(EXA_DIR)/*.asm)
EXA_PRG := $(EXA_SRC:.asm=.prg)
LIB_DIR := lib
AFLAGS  ?= --warn-unused
# --warn-unused-all


# Make
all: $(TARGET) $(SINGLE_TARGET) $(EXA_PRG)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

$(SINGLE_TARGET): $(SINGLE_SOURCE)
	touch $(SINGLE_SOURCE)
	$(CC) $(CFLAGS) -o $@ $(SINGLE_SOURCE)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cleanup
clean: clean_examples
	rm -f $(TARGET) $(SRC_DIR)/*.o $(SINGLE_TARGET)

clean_examples:
	rm -f $(EXA_PRG) $(EXA_DIR)/*.lst $(EXA_DIR)/*.vice $(EXA_DIR)/vice.log ./vice.log
	

# Assembly files to PRG
# 	@for file in $(EXA_PRG); do \
# 		new_file=$${file%.txt}.md; \
# 		echo "Converting $$file to $$new_file"; \
# 		mv "$$file" "$$new_file"; \
# 	done


test: 
	@for file in $(EXA_PRG); do \
		echo "*** TESTING $$file ***"; \
		python3 $(EXA_DIR)/mini6502.py "$$file"; \
	done


examples: $(TARGET) $(EXA_PRG)

$(EXA_PRG): $(EXA_SRC)


%.prg: %.asm
	touch $<
	$(CASM) $< -o $@ --lib-dir $(LIB_DIR) --listing $(<:.asm=.lst) --vice-labels $(<:.asm=.vice) $(AFLAGS) >> asm.log



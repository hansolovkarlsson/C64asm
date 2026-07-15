# Makefile for c64asm, built from its per-module source files.
#
#   make          builds ./c64asm
#   make clean    removes build artifacts
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

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)

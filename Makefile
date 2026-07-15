CC        := cc
CSTD      := -std=c99
WARN      := -Wall -Wextra
RELFLAGS  := $(CSTD) $(WARN) -O2
DBGFLAGS  := $(CSTD) $(WARN) -g -O0

SRC_DIR   := src
BIN_DIR   := bin
EX_DIR    := examples

SRC       := $(SRC_DIR)/c64asm.c
TARGET    := $(BIN_DIR)/c64asm

.PHONY: all debug clean run example

# Default build: optimized release binary in bin/
all: CFLAGS := $(RELFLAGS)
all: $(TARGET)

# Debug build: symbols, no optimization (for lldb / VS Code debugger)
debug: CFLAGS := $(DBGFLAGS)
debug: $(TARGET)

$(TARGET): $(SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Assemble the sample program in examples/ as a smoke test
example: all
	./$(TARGET) $(EX_DIR)/hello.asm -o $(BIN_DIR)/hello.prg --listing $(BIN_DIR)/hello.lst

old_clean:
	#rm -rf $(BIN_DIR)
	#mkdir -p $(BIN_DIR)
	#touch $(BIN_DIR)/.gitkeep
clean:
	rm $(TARGET)

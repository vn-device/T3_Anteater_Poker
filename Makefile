#==============================================================================
# File: Makefile
# Author: Team T3
# Date: May 14, 2026
# Description:
# Build automation for the Anteater Poker client application. 
# Handles GTK 3.0 dependencies, object compilation, and directory management.
#==============================================================================

# Compiler and standard flags
CC = gcc
CFLAGS = -Wall -std=c11 -g `pkg-config --cflags gtk+-3.0`

# Linker flags for GTK 3.0
LDFLAGS = `pkg-config --libs gtk+-3.0`

# Directories
SRC_DIR = src
BIN_DIR = bin

# Source and Object files
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/GameData.c $(SRC_DIR)/GameProtocol.c $(SRC_DIR)/GameGUI.c $(SRC_DIR)/HandEval.c
OBJS = $(SRCS:.c=.o)

# Output executable name
TARGET = $(BIN_DIR)/poker
TEST_TARGET = $(BIN_DIR)/hand_eval_tests
TEST_SRCS = tests/HandEvalTests.c $(SRC_DIR)/GameData.c $(SRC_DIR)/HandEval.c
TEST_CFLAGS = -Wall -std=c11 -g -I$(SRC_DIR)

#==============================================================================
# Build Rules
#==============================================================================

# Default target
all: $(BIN_DIR) $(TARGET)

# Create the binary directory if it does not exist
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Link object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile C source files into object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

#==============================================================================
# Utility Targets
#==============================================================================

# Remove all compiled objects and the binary directory
clean:
	rm -rf $(BIN_DIR) $(SRC_DIR)/*.o

# Placeholder for future automated testing execution
test: all
	@echo "Executing system tests..."
	# ./$(TARGET) --test-mode (To be implemented)

.PHONY: all clean test
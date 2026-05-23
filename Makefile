#==============================================================================
# File: Makefile
# Author: Team T3
# Date: May 22, 2026
# Description:
# Build automation for the Anteater Poker client and server applications.
# Handles GTK dependencies, isolated object compilation, and packaging.
#==============================================================================

# Compiler and standard diagnostic flags
CC = gcc
CFLAGS = -Wall -std=c11 -g `pkg-config --cflags gtk+-3.0`

# Linker flags for the GTK 3.0 Client
LDFLAGS_CLIENT = `pkg-config --libs gtk+-3.0`

# Directories
SRC_DIR = src
BIN_DIR = bin

# -----------------------------------------------------------------------------
# Client Subsystem Dependencies
# -----------------------------------------------------------------------------
CLIENT_SRCS = $(SRC_DIR)/main.c \
              $(SRC_DIR)/GameData.c \
              $(SRC_DIR)/GameProtocol.c \
              $(SRC_DIR)/GameGUI.c \
              $(SRC_DIR)/HandEval.c

CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_TARGET = $(BIN_DIR)/poker_client

# -----------------------------------------------------------------------------
# Server Subsystem Dependencies
# -----------------------------------------------------------------------------
SERVER_SRCS = $(SRC_DIR)/PokerServer.c \
              $(SRC_DIR)/GameData.c \
              $(SRC_DIR)/GameProtocol.c \
              $(SRC_DIR)/HandEval.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_TARGET = $(BIN_DIR)/poker_server

#==============================================================================
# Build Rules
#==============================================================================

# Default target invokes both client and server compilations
all: $(BIN_DIR) $(CLIENT_TARGET) $(SERVER_TARGET)

# Create the isolated binary storage directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Link the Client executable (Requires GTK dynamic libraries)
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_CLIENT)

# Link the Server executable (Strict C11, no GUI overhead)
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Pattern rule for compiling intermediate object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

#==============================================================================
# Mandatory Alpha Grading Targets
#==============================================================================

# Scrub intermediate objects and binary folder
clean:
	rm -rf $(BIN_DIR) $(SRC_DIR)/*.o

# Trigger headless logic/server tests
test: all
	@echo "Initializing headless server test..."
	./$(SERVER_TARGET)

# Trigger graphical interface tests
test-gui: all
	@echo "Initializing GTK client interface..."
	./$(CLIENT_TARGET)

# Generate compressed source archive for submission
tar: clean
	@echo "Generating Poker_Alpha_src.tar.gz..."
	tar -czvf Poker_Alpha_src.tar.gz $(SRC_DIR) doc Makefile README COPYRIGHT INSTALL

.PHONY: all clean test test-gui tar
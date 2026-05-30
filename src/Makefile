#==============================================================================
# File: src/Makefile
# Author: Team T3
# Date: May 24, 2026
# Description:
# Lower-level build automation. Handles GTK dependencies, object compilation,
# and linking executables into the parent bin/ directory.
#==============================================================================

# Compiler and standard diagnostic flags
CC = gcc
CFLAGS = -Wall -std=c11 -g `pkg-config --cflags gtk+-3.0`

# Linker flags for the GTK 3.0 Client
LDFLAGS_CLIENT = `pkg-config --libs gtk+-3.0` -lm

# -----------------------------------------------------------------------------
# Client Subsystem Dependencies
# -----------------------------------------------------------------------------
CLIENT_SRCS = main.c GameData.c GameProtocol.c GameGUI.c HandEval.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_TARGET = ../bin/poker_client

# -----------------------------------------------------------------------------
# Server Subsystem Dependencies
# -----------------------------------------------------------------------------
SERVER_SRCS = PokerServer.c GameData.c GameProtocol.c HandEval.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_TARGET = ../bin/poker_server

#==============================================================================
# Build Rules
#==============================================================================

all: $(CLIENT_TARGET) $(SERVER_TARGET)

# Link the Client executable
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS_CLIENT)

# Link the Server executable
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile C source files into objects
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Local object cleanup
clean:
	rm -f *.o

.PHONY: all clean
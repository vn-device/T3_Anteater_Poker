#==============================================================================
# File: Makefile
# Author: Team T3
# Date: May 24, 2026
# Description:
# Top-level build automation. Delegates compilation to the src/ directory
# and manages archive packaging for the Alpha Release.
#==============================================================================

# Default target creates the binary folder and delegates to the lower Makefile
all:
	mkdir -p bin
	$(MAKE) -C src all

# Scrubs the binary folder and delegates object cleanup to the lower Makefile
clean:
	rm -rf bin
	$(MAKE) -C src clean

# Trigger headless logic/server tests
test: all
	@echo "Initializing headless server test..."
	./bin/poker_server

# Trigger graphical interface tests
test-gui: all
	@echo "Initializing GTK client interface in offline mode..."
	./bin/poker_client --offline

# Generate compressed source archive for submission
tar: clean
	@echo "Generating Poker_Alpha.src.tar.gz..."
	tar -czvf Poker_Alpha.src.tar.gz src doc Makefile README COPYRIGHT INSTALL

# Generate compressed customer/end-user archive
tar-enduser: all
	@echo "Generating Poker_Alpha.tar.gz..."
	tar -czvf Poker_Alpha.tar.gz bin doc README COPYRIGHT INSTALL

.PHONY: all clean test test-gui tar tar-enduser
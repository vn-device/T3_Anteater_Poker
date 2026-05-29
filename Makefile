#==============================================================================
# File: Makefile
# Author: Team T3
# Date: May 25, 2026
# Description:
# Top-level build automation. Delegates compilation to the src/ directory
# and manages archive packaging for the Beta Release.
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
	./bin/poker_server --self-test

# Trigger graphical interface tests
test-gui: all
	@echo "Initializing GTK client interface in offline mode..."
	./bin/poker_client --offline

# Trigger client-server communication test
test-comm: all
	@echo "Initializing client-server communication test..."
	./bin/poker_server &
	sleep 1
	./bin/poker_bot TestBot botpass; kill %1

# Generate compressed source archive for submission
tar: all
	@echo "Generating Poker_Beta_src.tar.gz..."
	tar -czvf Poker_Beta_src.tar.gz src bin doc Makefile README COPYRIGHT INSTALL

# Generate compressed customer/end-user archive
tar-enduser: all
	@echo "Generating Poker_Beta.tar.gz..."
	tar -czvf Poker_Beta.tar.gz bin doc README COPYRIGHT INSTALL

.PHONY: all clean test test-gui test-comm tar tar-enduser

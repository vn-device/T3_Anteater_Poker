#==============================================================================
# File: Makefile
# Author: Team T3
# Date: May 30, 2026
# Description:
# Top-level build automation. Delegates compilation to the src/ directory
# and manages archive packaging for the Beta Release.
#==============================================================================

all:
	mkdir -p bin
	$(MAKE) -C src all

clean:
	rm -rf bin
	$(MAKE) -C src clean

test: all
	@echo "Initializing headless server test..."
	./bin/poker_server --self-test

test-gui: all
	@echo "Initializing GTK client interface in offline mode..."
	./bin/poker_client --offline

test-comm: all
	@echo "Executing client-server core protocol integration loop..."
	./bin/poker_server & SERVER_PID=$$!; \
	sleep 1; \
	./bin/poker_client --test-comm; \
	kill -9 $$SERVER_PID 2>/dev/null || true

tar: all
	@echo "Generating Poker_Beta_src.tar.gz..."
	tar -czvf Poker_Beta_src.tar.gz src bin doc Makefile README COPYRIGHT INSTALL

tar-enduser: all
	@echo "Generating Poker_Beta.tar.gz..."
	tar --exclude='doc/*[Ss]oftware*[Ss]pec*' -czvf Poker_Beta.tar.gz bin doc README COPYRIGHT INSTALL

.PHONY: all clean test test-gui test-comm tar tar-enduser
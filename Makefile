# Makefile for Content-Aware Caching Algorithm

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = 

# Main targets
all: caching_system test_cache

# Main executable
caching_system: main.cpp content_aware_cache.cpp content_aware_cache.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp content_aware_cache.cpp $(LDFLAGS)

# Test program
test_cache: test_cache.cpp content_aware_cache.cpp content_aware_cache.h
	$(CXX) $(CXXFLAGS) -o $@ test_cache.cpp content_aware_cache.cpp $(LDFLAGS)

# Clean up
clean:
	rm -f caching_system test_cache *.o

# Run tests
test: test_cache
	./test_cache

# Run main program
run: caching_system
	./caching_system

.PHONY: all clean test run

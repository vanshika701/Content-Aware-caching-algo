# Makefile for Content-Aware Caching Algorithm (Java)

JAVAC = javac
JAVA = java
SRC = $(wildcard src/cache/*.java)
OUT = out

# Main targets
all: build

# Compile all sources
build: $(SRC)
	$(JAVAC) -d $(OUT) $(SRC)

# Run the LRU vs content-aware benchmark (optional: make test SEED=42)
test: build
	$(JAVA) -cp $(OUT) cache.TestCache $(SEED)

# Run the interactive command-line interface
run: build
	$(JAVA) -cp $(OUT) cache.Main

# Clean up
clean:
	rm -rf $(OUT) test_files

.PHONY: all build test run clean

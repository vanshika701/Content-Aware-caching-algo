# Content-Aware Caching Algorithm for Efficient File Access

## Overview

This project implements a heuristic-based Content-Aware Caching Algorithm that optimizes file caching decisions based on file type, access frequency, and size. Unlike traditional caching techniques like LRU (Least Recently Used) and LFU (Least Frequently Used), this approach prioritizes files dynamically based on their relevance and historical usage patterns.

## Features

- **Content-Aware Prioritization**: Intelligently prioritizes files based on:
  - File type (extension)
  - File size (favors smaller files for better cache utilization)
  - Access frequency (adapts to usage patterns)
  - Recency of access (time decay factor)

- **Configurable Type Priorities**: Allows setting different priorities for different file types based on expected access patterns

- **Performance Monitoring**: Tracks and reports detailed statistics on:
  - Cache hit rate
  - Disk I/O operations
  - Cache utilization

- **Comparison Testing**: Built-in framework to compare against traditional LRU implementation

## Implementation Details

The caching system is implemented in C++ as a user-space library that provides file I/O operations through a caching layer. The core components include:

- **ContentAwareCache**: Main cache manager that handles file storage, retrieval, and eviction decisions
- **CacheFile**: File handle for cached files, similar to FILE* in standard I/O
- **Test Framework**: Tools to generate test data and measure performance

## Project Structure

```
├── content_aware_cache.h     # Core cache implementation header
├── content_aware_cache.cpp   # Implementation of the cache
├── main.cpp                  # Interactive command-line interface
├── test_cache.cpp            # Performance testing framework
├── Makefile                  # Build configuration
└── README.md                 # This documentation
```

## Building and Running

### Prerequisites

- C++17 compatible compiler (GCC or Clang)
- Standard C++ libraries including filesystem support

### Build Instructions

```bash
# Build everything
make all

# Build just the main program
make caching_system

# Build just the test program
make test_cache
```

### Running the Program

```bash
# Run the interactive command-line interface
./caching_system

# Run the test suite
./test_cache
```

## Usage

The command-line interface supports the following commands:

- `read <filename>` - Read a file through the cache
- `write <filename> <content>` - Write content to a file through the cache
- `append <filename> <content>` - Append content to a file through the cache
- `flush` - Flush all changes to disk
- `clear` - Clear the cache
- `stats` - Show cache statistics
- `resize <size_mb>` - Resize the cache (in MB)
- `priority <ext> <value>` - Set priority for file type (0.0-1.0)
- `help` - Show help information
- `exit` - Exit the program

## Performance Evaluation

The test framework (`test_cache.cpp`) generates random test files and access patterns to measure the performance of the content-aware caching algorithm compared to standard LRU caching. It reports:

- Cache hit rates
- Disk I/O operations
- Execution time

## Extending the Project

Possible extensions include:

1. Implementing a FUSE (Filesystem in Userspace) layer for transparent integration
2. Adding more sophisticated heuristics for access pattern recognition
3. Developing an adaptive learning component to automatically adjust priorities
4. Creating a Linux kernel module version for deeper OS integration

## Authors

- Aditya Sharma (2310110390)
- Vanshika Srivastava (2310110339)
- Shruti Sharma (2310110715)
- Vatsal Saxena (2310110345)

## License

This project is released under the MIT License.

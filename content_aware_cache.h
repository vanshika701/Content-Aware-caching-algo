// content_aware_cache.h
#ifndef CONTENT_AWARE_CACHE_H
#define CONTENT_AWARE_CACHE_H

#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

// Struct to store file metadata
struct FileMetadata {
    std::string filePath;
    std::string fileType;
    size_t fileSize;
    decltype(std::filesystem::last_write_time(".")) lastModified;

};

// Struct to track file access statistics
struct AccessStats {
    size_t accessCount;
    std::chrono::system_clock::time_point lastAccessed;
    
    AccessStats() : accessCount(0) {
        lastAccessed = std::chrono::system_clock::now();
    }
};

// Cache entry representing a file in cache
class CacheEntry {
public:
    FileMetadata metadata;
    AccessStats stats;
    std::vector<char> data;
    float priorityScore;
    
    CacheEntry(const FileMetadata& meta) : metadata(meta), priorityScore(0.0f) {}
    
    size_t getMemoryUsage() const {
        return data.size();
    }
};

// File handle for cached files
class CacheFile {
private:
    std::shared_ptr<CacheEntry> entry;
    size_t position;
    std::string mode;
    bool modified;
    std::weak_ptr<class ContentAwareCache> cachePtr;
    
public:
    CacheFile(std::shared_ptr<CacheEntry> entry, const std::string& mode, 
              std::weak_ptr<class ContentAwareCache> cache)
        : entry(entry), position(0), mode(mode), modified(false), cachePtr(cache) {}
    
    ~CacheFile();
    
    size_t read(void* buffer, size_t size, size_t count);
    size_t write(const void* buffer, size_t size, size_t count);
    int seek(long offset, int origin);
    long tell();
    int flush();
};

// Main cache manager class
class ContentAwareCache : public std::enable_shared_from_this<ContentAwareCache> {
private:
    // Maximum cache size in bytes
    size_t maxCacheSize;
    // Current cache size in bytes
    size_t currentCacheSize;
    
    // Cache storage: map from file path to cache entry
    std::unordered_map<std::string, std::shared_ptr<CacheEntry>> cacheMap;
    
    // LRU list for basic eviction policy backup
    std::list<std::string> lruList;
    std::unordered_map<std::string, std::list<std::string>::iterator> lruMap;
    
    // Statistics
    size_t cacheHits;
    size_t cacheMisses;
    size_t diskReads;
    size_t diskWrites;
    
    // Thread safety
    std::mutex cacheMutex;
    
    // File type priority weights (configurable)
    std::unordered_map<std::string, float> fileTypePriorities;
    
    // Helper methods
    FileMetadata getFileMetadata(const std::string& filePath);
    float calculatePriorityScore(const std::shared_ptr<CacheEntry>& entry);
    void updateLRU(const std::string& filePath);
    std::string findEntryForEviction();
    bool loadFileIntoCache(const std::string& filePath);
    void evictFile(const std::string& filePath);
    void makeRoomInCache(size_t requiredSize);
    void updateEntryScore(const std::string& filePath);
    void updateAllScores();
    
public:
    ContentAwareCache(size_t maxSize = 64 * 1024 * 1024);  // Default 64MB cache
    ~ContentAwareCache();
    
    // File operations
    CacheFile* openFile(const std::string& filePath, const std::string& mode);
    bool closeFile(CacheFile* file);
    
    // Cache management
    void flush();
    void clear();
    void resizeCache(size_t newMaxSize);
    
    // Priority configuration
    void setFileTypePriority(const std::string& extension, float priority);
    
    // Statistics
    float getHitRate() const;
    size_t getDiskReadCount() const { return diskReads; }
    size_t getDiskWriteCount() const { return diskWrites; }
    void printStats() const;
    
    // For testing
    size_t getCacheSize() const { return currentCacheSize; }
    size_t getCacheEntryCount() const { return cacheMap.size(); }
    
    friend class CacheFile;
};

#endif // CONTENT_AWARE_CACHE_H

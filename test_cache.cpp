// test_cache.cpp
#include "content_aware_cache.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// Utility Functions
void createTestFile(const std::string& filePath, size_t size, char fillChar = 'A') {
    std::ofstream file(filePath, std::ios::binary);
    std::vector<char> buffer(size, fillChar);
    file.write(buffer.data(), size);
}

void createTestDirectory(const std::string& dirPath) {
    fs::create_directories(dirPath);
}

void cleanTestDirectory(const std::string& dirPath) {
    if (fs::exists(dirPath)) {
        fs::remove_all(dirPath);
    }
}

// Enhanced Test Data Generator
class TestDataGenerator {
public:
    struct FileTypeInfo {
        std::string extension;
        size_t minSize;
        size_t maxSize;
        float importance;
    };

private:
    std::mt19937 rng;
    std::uniform_int_distribution<int> charDist;
    std::string testDir;

    
    std::vector<FileTypeInfo> fileTypes;
    
public:
    TestDataGenerator(const std::string& dir)
        : rng(std::random_device{}()), 
          charDist(65, 90),  // A-Z
          testDir(dir) {
        
        createTestDirectory(testDir);
        
        // Define various file types with different size ranges and importance
        fileTypes = {
            {".cfg", 1 * 1024, 10 * 1024, 0.9f},      // Small config files (high importance)
            {".xml", 5 * 1024, 50 * 1024, 0.8f},      // Medium XML files (high importance)
            {".json", 2 * 1024, 30 * 1024, 0.8f},     // Small-medium JSON files (high importance)
            {".log", 100 * 1024, 500 * 1024, 0.6f},   // Larger log files (medium importance)
            {".txt", 1 * 1024, 100 * 1024, 0.7f},     // Text files of various sizes (medium importance)
            {".dat", 200 * 1024, 1024 * 1024, 0.4f},  // Large data files (lower importance)
            {".bin", 500 * 1024, 2 * 1024 * 1024, 0.3f}, // Large binary files (lower importance)
            {".tmp", 10 * 1024, 100 * 1024, 0.2f}     // Temporary files (lowest importance)
        };
    }
    
    ~TestDataGenerator() {
        cleanTestDirectory(testDir);
    }
    
    std::string generateFile(size_t typeIndex, size_t fileIndex) {
        const FileTypeInfo& typeInfo = fileTypes[typeIndex % fileTypes.size()];
        std::string name = "file_" + std::to_string(fileIndex);
        std::string filePath = testDir + "/" + name + typeInfo.extension;
        
        // Generate random size within the range for this file type
        std::uniform_int_distribution<size_t> sizeDist(typeInfo.minSize, typeInfo.maxSize);
        size_t size = sizeDist(rng);
        
        char fillChar = static_cast<char>(charDist(rng));
        createTestFile(filePath, size, fillChar);
        
        return filePath;
    }
    
    std::vector<std::string> generateTestSet(size_t count) {
        std::vector<std::string> files;
        
        // Distribute files among different types
        for (size_t i = 0; i < count; i++) {
            size_t typeIndex = i % fileTypes.size();
            files.push_back(generateFile(typeIndex, i));
        }
        
        return files;
    }
    
    const std::vector<FileTypeInfo>& getFileTypes() const {
        return fileTypes;
    }
};

// Enhanced LRU Cache implementation for comparison
class LRUCache {
private:
    struct CacheItem {
        std::string filePath;
        std::vector<char> data;
    };
    
    size_t maxCacheSize;
    size_t currentCacheSize;
    std::list<std::string> lruList;
    std::unordered_map<std::string, std::pair<CacheItem, std::list<std::string>::iterator>> cache;
    
    size_t cacheHits;
    size_t cacheMisses;
    size_t diskReads;
    
public:
    LRUCache(size_t maxSize) 
        : maxCacheSize(maxSize), currentCacheSize(0), 
          cacheHits(0), cacheMisses(0), diskReads(0) {}
    
    bool accessFile(const std::string& filePath) {
        auto it = cache.find(filePath);
        
        if (it != cache.end()) {
            // Cache hit
            cacheHits++;
            
            // Move to front of LRU list
            lruList.erase(it->second.second);
            lruList.push_front(filePath);
            it->second.second = lruList.begin();
            
            return true;
        }
        
        // Cache miss
        cacheMisses++;
        
        // Read file
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Make room in cache if needed
        while (currentCacheSize + fileSize > maxCacheSize && !lruList.empty()) {
            std::string victimPath = lruList.back();
            currentCacheSize -= cache[victimPath].first.data.size();
            cache.erase(victimPath);
            lruList.pop_back();
        }
        
        // Add to cache if it fits
        if (fileSize <= maxCacheSize) {
            CacheItem item;
            item.filePath = filePath;
            item.data.resize(fileSize);
            
            file.read(item.data.data(), fileSize);
            diskReads++;
            
            lruList.push_front(filePath);
            cache[filePath] = {item, lruList.begin()};
            currentCacheSize += fileSize;
        } else {
            // File too large for cache, just read it
            diskReads++;
        }
        
        return true;
    }
    
    float getHitRate() const {
        size_t totalAccesses = cacheHits + cacheMisses;
        if (totalAccesses == 0) {
            return 0.0f;
        }
        return static_cast<float>(cacheHits) / static_cast<float>(totalAccesses);
    }
    
    size_t getDiskReadCount() const { return diskReads; }
    size_t getCacheHits() const { return cacheHits; }
    size_t getCacheMisses() const { return cacheMisses; }
    size_t getCacheSize() const { return currentCacheSize; }
    size_t getCacheEntryCount() const { return cache.size(); }
};

// Enhanced workload generator with realistic patterns
class WorkloadGenerator {
private:
    std::mt19937 rng;
    const std::vector<std::string>& files;
    std::vector<std::string> fileTypes; // Store file extensions
    
public:
    WorkloadGenerator(const std::vector<std::string>& fileSet) 
        : rng(std::random_device{}()), files(fileSet) {
        
        // Extract file types from paths
        for (const auto& file : files) {
            fs::path path(file);
            fileTypes.push_back(path.extension().string());
        }
    }
    
    // Generate workload with three phases to simulate real application behavior
    std::vector<std::string> generateRealisticWorkload(size_t totalAccesses) {
        std::vector<std::string> workload;
        
        // Phase 1 (30%): Application startup - config files loaded, initial resources accessed
        size_t startupPhase = totalAccesses * 0.3;
        workload.reserve(totalAccesses);
        
        // Config files accessed first
        for (size_t i = 0; i < files.size(); i++) {
            if (fileTypes[i] == ".cfg" || fileTypes[i] == ".json" || fileTypes[i] == ".xml") {
                // Access config files multiple times during startup
                for (int j = 0; j < 5 && workload.size() < startupPhase; j++) {
                    workload.push_back(files[i]);
                }
            }
        }
        
        // Fill remaining startup phase with random accesses
        std::uniform_int_distribution<size_t> fileDist(0, files.size() - 1);
        while (workload.size() < startupPhase) {
            workload.push_back(files[fileDist(rng)]);
        }
        
        // Phase 2 (60%): Normal operation - locality with occasional bursts
        size_t operationPhase = totalAccesses * 0.6;
        size_t normalOpEnd = startupPhase + operationPhase;
        
        // Set up clusters of related file accesses
        size_t clusterSize = 5;
        while (workload.size() < normalOpEnd) {
            // Choose a random starting file
            size_t baseFile = fileDist(rng);
            
            // Create a cluster of accesses around this file and similar types
            for (size_t i = 0; i < clusterSize && workload.size() < normalOpEnd; i++) {
                // Sometimes access the base file
                if (i % 2 == 0) {
                    workload.push_back(files[baseFile]);
                } else {
                    // Find a file with the same extension
                    std::string targetExt = fileTypes[baseFile];
                    std::vector<size_t> sameTypeFiles;
                    
                    for (size_t j = 0; j < files.size(); j++) {
                        if (fileTypes[j] == targetExt) {
                            sameTypeFiles.push_back(j);
                        }
                    }
                    
                    if (!sameTypeFiles.empty()) {
                        std::uniform_int_distribution<size_t> sameDist(0, sameTypeFiles.size() - 1);
                        workload.push_back(files[sameTypeFiles[sameDist(rng)]]);
                    } else {
                        workload.push_back(files[fileDist(rng)]);
                    }
                }
            }
            
            // Occasionally access some random files (context switch)
            if (std::uniform_real_distribution<float>(0, 1)(rng) < 0.3) {
                for (size_t i = 0; i < 3 && workload.size() < normalOpEnd; i++) {
                    workload.push_back(files[fileDist(rng)]);
                }
            }
        }
        
        // Phase 3 (10%): Application wind-down - log files, cleanup
        while (workload.size() < totalAccesses) {
            // Higher probability of accessing log files
            bool accessLog = std::uniform_real_distribution<float>(0, 1)(rng) < 0.6;
            
            if (accessLog) {
                // Find and access a log file
                std::vector<size_t> logFiles;
                for (size_t i = 0; i < files.size(); i++) {
                    if (fileTypes[i] == ".log") {
                        logFiles.push_back(i);
                    }
                }
                
                if (!logFiles.empty()) {
                    std::uniform_int_distribution<size_t> logDist(0, logFiles.size() - 1);
                    workload.push_back(files[logFiles[logDist(rng)]]);
                } else {
                    workload.push_back(files[fileDist(rng)]);
                }
            } else {
                // Random access to any file
                workload.push_back(files[fileDist(rng)]);
            }
        }
        
        return workload;
    }
    
    // Specific pattern that heavily favors important files
    std::vector<std::string> generateImportantFilesBurstWorkload(size_t totalAccesses) {
        std::vector<std::string> workload;
        workload.reserve(totalAccesses);
        
        std::uniform_int_distribution<size_t> fileDist(0, files.size() - 1);
        
        // Group files by extension
        std::unordered_map<std::string, std::vector<size_t>> filesByType;
        for (size_t i = 0; i < files.size(); i++) {
            filesByType[fileTypes[i]].push_back(i);
        }
        
        // List of important extensions in order of importance
        std::vector<std::string> importantExts = {".cfg", ".json", ".xml", ".txt"};
        
        size_t pos = 0;
        while (pos < totalAccesses) {
            // Random extension burst (70% of accesses)
            std::string burstExt = importantExts[std::uniform_int_distribution<size_t>(0, importantExts.size() - 1)(rng)];
            
            // If we have files of this type
            if (!filesByType[burstExt].empty()) {
                // Determine burst length (5-20 accesses)
                size_t burstLength = std::uniform_int_distribution<size_t>(5, 20)(rng);
                burstLength = std::min(burstLength, totalAccesses - pos);
                
                for (size_t i = 0; i < burstLength; i++) {
                    // Pick random file of this type
                    size_t fileIndex = filesByType[burstExt][std::uniform_int_distribution<size_t>(0, filesByType[burstExt].size() - 1)(rng)];
                    workload.push_back(files[fileIndex]);
                    pos++;
                }
            }
            
            // Random accesses (30% of total)
            size_t randomLength = std::uniform_int_distribution<size_t>(1, 5)(rng);
            randomLength = std::min(randomLength, totalAccesses - pos);
            
            for (size_t i = 0; i < randomLength; i++) {
                workload.push_back(files[fileDist(rng)]);
                pos++;
            }
        }
        
        return workload;
    }
};

// Test function for standard caching
void testStandardCaching(const std::vector<std::string>& workload, size_t cacheSize) {
    std::cout << "Testing standard LRU caching..." << std::endl;
    
    LRUCache lruCache(cacheSize);
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (const auto& filePath : workload) {
        lruCache.accessFile(filePath);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "LRU Results:" << std::endl;
    std::cout << "  Cache Size: " << lruCache.getCacheSize() << " / " << cacheSize << " bytes" << std::endl;
    std::cout << "  Cache Entries: " << lruCache.getCacheEntryCount() << std::endl;
    std::cout << "  Cache Hits: " << lruCache.getCacheHits() << std::endl;
    std::cout << "  Cache Misses: " << lruCache.getCacheMisses() << std::endl;
    std::cout << "  Hit Rate: " << (lruCache.getHitRate() * 100.0f) << "%" << std::endl;
    std::cout << "  Disk Reads: " << lruCache.getDiskReadCount() << std::endl;
    std::cout << "  Execution Time: " << duration.count() << "ms" << std::endl;
}

// Test function for content-aware caching
void testContentAwareCaching(const std::vector<std::string>& workload, size_t cacheSize, 
                            const std::vector<TestDataGenerator::FileTypeInfo>& fileTypes) {
    std::cout << "Testing content-aware caching..." << std::endl;
    
    auto cache = std::make_shared<ContentAwareCache>(cacheSize);
    
    // Set file type priorities based on the file type information
    for (const auto& type : fileTypes) {
        cache->setFileTypePriority(type.extension, type.importance);
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (const auto& filePath : workload) {
        CacheFile* file = cache->openFile(filePath, "r");
        if (file) {
            // Read a small amount to simulate file access
            char buffer[1024];
            file->read(buffer, 1, sizeof(buffer));
            cache->closeFile(file);
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Content-Aware Results:" << std::endl;
    cache->printStats();
    std::cout << "  Execution Time: " << duration.count() << "ms" << std::endl;
}

// Main program
int main() {
    std::cout << "Content-Aware Caching Algorithm Test" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // Create test files with diverse types and sizes
    TestDataGenerator generator("./test_files");
    std::vector<std::string> testFiles = generator.generateTestSet(100); // Increase to 100 files
    
    std::cout << "Created " << testFiles.size() << " test files." << std::endl;
    
    // Create workload generator
    WorkloadGenerator workloadGen(testFiles);
    
    // Generate a realistic workload
    std::vector<std::string> realisticWorkload = workloadGen.generateRealisticWorkload(20000);
    
    std::cout << "Generated realistic workload of " << realisticWorkload.size() << " file accesses." << std::endl;
    
    // Set a smaller cache size to force eviction decisions
    // Use approximately 25% of what would be needed to cache all files
    size_t estimatedTotalSize = 0;
    for (const auto& filePath : testFiles) {
        try {
            estimatedTotalSize += fs::file_size(filePath);
        } catch (...) {
            // Ignore errors
        }
    }
    size_t cacheSize = estimatedTotalSize / 4;
    
    std::cout << "Using cache size of " << (cacheSize / 1024 / 1024) << " MB" << std::endl;
    std::cout << "(Approximately 25% of total data size)" << std::endl;
    
    // Test standard LRU caching
    testStandardCaching(realisticWorkload, cacheSize);
    
    std::cout << std::endl;
    
    // Test content-aware caching
    testContentAwareCaching(realisticWorkload, cacheSize, generator.getFileTypes());
    
    std::cout << "\n--- Additional Test: Important Files Burst Pattern ---\n" << std::endl;
    
    // Generate workload with bursts of important file accesses
    std::vector<std::string> burstWorkload = workloadGen.generateImportantFilesBurstWorkload(10000);
    
    std::cout << "Generated important-files burst workload of " << burstWorkload.size() << " file accesses." << std::endl;
    
    // Test standard LRU caching
    testStandardCaching(burstWorkload, cacheSize);
    
    std::cout << std::endl;
    
    // Test content-aware caching
    testContentAwareCaching(burstWorkload, cacheSize, generator.getFileTypes());
    
    return 0;
}
// content_aware_cache.cpp
#include "content_aware_cache.h"
#include <algorithm>
#include <cmath>

// CacheFile implementation
CacheFile::~CacheFile() {
    // Flush changes if needed
    if (modified) {
        flush();
    }
    
    // Update access stats
    if (auto cache = cachePtr.lock()) {
        std::lock_guard<std::mutex> lock(cache->cacheMutex);
        entry->stats.accessCount++;
        entry->stats.lastAccessed = std::chrono::system_clock::now();
        cache->updateEntryScore(entry->metadata.filePath);
    }
}

size_t CacheFile::read(void* buffer, size_t size, size_t count) {
    if (mode.find('r') == std::string::npos) {
        // Not opened for reading
        return 0;
    }
    
    size_t bytesToRead = size * count;
    size_t bytesAvailable = entry->data.size() - position;
    size_t bytesToCopy = std::min(bytesToRead, bytesAvailable);
    
    if (bytesToCopy > 0) {
        std::memcpy(buffer, entry->data.data() + position, bytesToCopy);
        position += bytesToCopy;
    }
    
    return bytesToCopy / size;  // Return count of items read
}

size_t CacheFile::write(const void* buffer, size_t size, size_t count) {
    if (mode.find('w') == std::string::npos && mode.find('a') == std::string::npos) {
        // Not opened for writing
        return 0;
    }
    
    size_t bytesToWrite = size * count;
    
    // If appending, move to the end
    if (mode.find('a') != std::string::npos && position != entry->data.size()) {
        position = entry->data.size();
    }
    
    // Check if we need to resize the buffer
    if (position + bytesToWrite > entry->data.size()) {
        // Get the cache to ensure we have space
        if (auto cache = cachePtr.lock()) {
            // Request additional space
            size_t oldSize = entry->data.size();
            size_t newSize = position + bytesToWrite;
            size_t additionalSpace = newSize - oldSize;
            
            std::lock_guard<std::mutex> lock(cache->cacheMutex);
            cache->makeRoomInCache(additionalSpace);
            entry->data.resize(newSize);
            cache->currentCacheSize += additionalSpace;
        }
    }
    
    // Copy the data
    std::memcpy(entry->data.data() + position, buffer, bytesToWrite);
    position += bytesToWrite;
    modified = true;
    
    return count;  // Return count of items written
}

int CacheFile::seek(long offset, int origin) {
    size_t newPosition;
    
    switch (origin) {
        case SEEK_SET:
            newPosition = offset;
            break;
        case SEEK_CUR:
            newPosition = position + offset;
            break;
        case SEEK_END:
            newPosition = entry->data.size() + offset;
            break;
        default:
            return -1;
    }
    
    if (newPosition > entry->data.size()) {
        // Cannot seek beyond end of file
        return -1;
    }
    
    position = newPosition;
    return 0;
}

long CacheFile::tell() {
    return static_cast<long>(position);
}

int CacheFile::flush() {
    if (!modified) {
        return 0;
    }
    
    // Write back to disk
    std::ofstream file(entry->metadata.filePath, std::ios::binary);
    if (!file) {
        return -1;
    }
    
    file.write(entry->data.data(), entry->data.size());
    if (!file) {
        return -1;
    }
    
    if (auto cache = cachePtr.lock()) {
        std::lock_guard<std::mutex> lock(cache->cacheMutex);
        cache->diskWrites++;
    }
    
    modified = false;
    return 0;
}

// ContentAwareCache implementation
ContentAwareCache::ContentAwareCache(size_t maxSize) 
    : maxCacheSize(maxSize), currentCacheSize(0),
      cacheHits(0), cacheMisses(0), diskReads(0), diskWrites(0) {
    
    // Setup default file type priorities
    fileTypePriorities = {
        {".txt", 0.7f},
        {".cfg", 0.9f},
        {".conf", 0.9f},
        {".ini", 0.9f},
        {".log", 0.6f},
        {".json", 0.8f},
        {".xml", 0.8f},
        {".cpp", 0.7f},
        {".h", 0.7f},
        {".c", 0.7f},
        {".py", 0.7f},
        {".jpg", 0.4f},
        {".png", 0.4f},
        {".pdf", 0.3f},
        {".exe", 0.1f},
        {".so", 0.1f},
        {".dll", 0.1f}
    };
}

ContentAwareCache::~ContentAwareCache() {
    flush();
}

FileMetadata ContentAwareCache::getFileMetadata(const std::string& filePath) {
    FileMetadata metadata;
    metadata.filePath = filePath;
    
    try {
        fs::path path(filePath);
        metadata.fileType = path.extension().string();
        metadata.fileSize = fs::file_size(path);
metadata.lastModified = fs::last_write_time(path);

    } catch (const std::exception& e) {
        std::cerr << "Error reading file metadata: " << e.what() << std::endl;
        metadata.fileSize = 0;
         metadata.lastModified = std::filesystem::file_time_type::clock::now(); // Ensure type compatibility
    }
    
    return metadata;
}

float ContentAwareCache::calculatePriorityScore(const std::shared_ptr<CacheEntry>& entry) {
    // Higher score = higher priority to keep in cache
    float score = 0.0f;
    
    // Factor 1: File type priority (0.0-1.0)
    float typePriority = 0.5f;  // Default for unknown types
    auto it = fileTypePriorities.find(entry->metadata.fileType);
    if (it != fileTypePriorities.end()) {
        typePriority = it->second;
    }
    
    // Factor 2: File size (favor smaller files)
    // 1.0 for files < 1KB, decreasing for larger files
    float sizeScore = 1.0f;
    if (entry->metadata.fileSize > 1024) {
        sizeScore = std::min(1.0f, 10240.0f / static_cast<float>(entry->metadata.fileSize));
    }
    
    // Factor 3: Access frequency 
    // Log scale: more accesses = higher score
    float accessScore = 0.1f + std::min(0.9f, std::log2(1.0f + entry->stats.accessCount) / 10.0f);
    
    // Factor 4: Recency of access
    // Score decreases as time since last access increases
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - entry->stats.lastAccessed).count();
    float recencyScore = std::exp(-duration / 3600.0f); // Decay over ~1 hour
    
    // Combine factors with weights
    score = (typePriority * 0.3f) + (sizeScore * 0.2f) + 
            (accessScore * 0.3f) + (recencyScore * 0.2f);
    
    return score;
}

void ContentAwareCache::updateLRU(const std::string& filePath) {
    auto it = lruMap.find(filePath);
    
    if (it != lruMap.end()) {
        // Move to front of LRU list
        lruList.erase(it->second);
    }
    
    lruList.push_front(filePath);
    lruMap[filePath] = lruList.begin();
}

std::string ContentAwareCache::findEntryForEviction() {
    if (cacheMap.empty()) {
        return "";
    }
    
    // Find entry with lowest priority score
    std::string candidatePath = "";
    float lowestScore = std::numeric_limits<float>::max();
    
    for (const auto& pair : cacheMap) {
        if (pair.second->priorityScore < lowestScore) {
            lowestScore = pair.second->priorityScore;
            candidatePath = pair.first;
        }
    }
    
    // Fallback to LRU if all scores are the same
    if (candidatePath.empty() && !lruList.empty()) {
        candidatePath = lruList.back();
    }
    
    return candidatePath;
}

bool ContentAwareCache::loadFileIntoCache(const std::string& filePath) {
    FileMetadata metadata = getFileMetadata(filePath);
    if (metadata.fileSize == 0) {
        return false;
    }
    
    // Make room in cache if needed
    makeRoomInCache(metadata.fileSize);
    
    // Read file into memory
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }
    
    auto entry = std::make_shared<CacheEntry>(metadata);
    entry->data.resize(metadata.fileSize);
    
    file.read(entry->data.data(), metadata.fileSize);
    if (!file && !file.eof()) {
        return false;
    }
    
    // Update cache
    cacheMap[filePath] = entry;
    currentCacheSize += metadata.fileSize;
    updateLRU(filePath);
    diskReads++;
    
    // Calculate initial score
    entry->priorityScore = calculatePriorityScore(entry);
    
    return true;
}

void ContentAwareCache::evictFile(const std::string& filePath) {
    auto it = cacheMap.find(filePath);
    if (it == cacheMap.end()) {
        return;
    }
    
    // Flush changes if needed
    CacheEntry& entry = *(it->second);
    
    // Update cache size
    currentCacheSize -= entry.getMemoryUsage();
    
    // Remove from LRU
    auto lruIt = lruMap.find(filePath);
    if (lruIt != lruMap.end()) {
        lruList.erase(lruIt->second);
        lruMap.erase(lruIt);
    }
    
    // Remove from cache
    cacheMap.erase(it);
}

void ContentAwareCache::makeRoomInCache(size_t requiredSize) {
    // Quick return if we have enough space
    if (currentCacheSize + requiredSize <= maxCacheSize) {
        return;
    }
    
    // Update all scores before eviction
    updateAllScores();
    
    // Evict files until we have enough space
    while (currentCacheSize + requiredSize > maxCacheSize && !cacheMap.empty()) {
        std::string victimPath = findEntryForEviction();
        if (victimPath.empty()) {
            break;
        }
        evictFile(victimPath);
    }
    
    // If still not enough space, increase max cache size
    if (currentCacheSize + requiredSize > maxCacheSize) {
        maxCacheSize = currentCacheSize + requiredSize;
    }
}

void ContentAwareCache::updateEntryScore(const std::string& filePath) {
    auto it = cacheMap.find(filePath);
    if (it != cacheMap.end()) {
        it->second->priorityScore = calculatePriorityScore(it->second);
    }
}

void ContentAwareCache::updateAllScores() {
    for (auto& pair : cacheMap) {
        pair.second->priorityScore = calculatePriorityScore(pair.second);
    }
}

CacheFile* ContentAwareCache::openFile(const std::string& filePath, const std::string& mode) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Check if file is already in cache
    auto it = cacheMap.find(filePath);
    if (it != cacheMap.end()) {
        // File is in cache
        cacheHits++;
        updateLRU(filePath);
        return new CacheFile(it->second, mode, weak_from_this());
    }
    
    // File not in cache
    cacheMisses++;
    
    // Check if file exists for reading
    if (mode.find('r') != std::string::npos && !fs::exists(filePath)) {
        return nullptr;
    }
    
    // Create empty file for writing
    if (mode.find('w') != std::string::npos) {
        FileMetadata metadata = getFileMetadata(filePath);
        metadata.fileSize = 0; // Start with empty file
        
        auto entry = std::make_shared<CacheEntry>(metadata);
        cacheMap[filePath] = entry;
        updateLRU(filePath);
        
        return new CacheFile(entry, mode, weak_from_this());
    }
    
    // Load existing file for reading or appending
    if (loadFileIntoCache(filePath)) {
        return new CacheFile(cacheMap[filePath], mode, weak_from_this());
    }
    
    return nullptr;
}

bool ContentAwareCache::closeFile(CacheFile* file) {
    if (file) {
        delete file;
        return true;
    }
    return false;
}

void ContentAwareCache::flush() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (auto& pair : cacheMap) {
        auto entry = pair.second;
        
        std::ofstream file(entry->metadata.filePath, std::ios::binary);
        if (file) {
            file.write(entry->data.data(), entry->data.size());
            if (file) {
                diskWrites++;
            }
        }
    }
}

void ContentAwareCache::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    flush();  // Write all changes to disk first
    
    cacheMap.clear();
    lruList.clear();
    lruMap.clear();
    currentCacheSize = 0;
}

void ContentAwareCache::resizeCache(size_t newMaxSize) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    if (newMaxSize < maxCacheSize) {
        // Need to evict some files
        makeRoomInCache(maxCacheSize - newMaxSize);
    }
    
    maxCacheSize = newMaxSize;
}

void ContentAwareCache::setFileTypePriority(const std::string& extension, float priority) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Ensure extension starts with a dot
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    
    fileTypePriorities[ext] = std::max(0.0f, std::min(1.0f, priority));
    
    // Update scores for files of this type
    for (auto& pair : cacheMap) {
        if (pair.second->metadata.fileType == ext) {
            pair.second->priorityScore = calculatePriorityScore(pair.second);
        }
    }
}

float ContentAwareCache::getHitRate() const {
    size_t totalAccesses = cacheHits + cacheMisses;
    if (totalAccesses == 0) {
        return 0.0f;
    }
    return static_cast<float>(cacheHits) / static_cast<float>(totalAccesses);
}

void ContentAwareCache::printStats() const {
    std::cout << "Cache Statistics:" << std::endl;
    std::cout << "  Cache Size: " << currentCacheSize << " / " << maxCacheSize << " bytes" << std::endl;
    std::cout << "  Cache Entries: " << cacheMap.size() << std::endl;
    std::cout << "  Cache Hits: " << cacheHits << std::endl;
    std::cout << "  Cache Misses: " << cacheMisses << std::endl;
    std::cout << "  Hit Rate: " << (getHitRate() * 100.0f) << "%" << std::endl;
    std::cout << "  Disk Reads: " << diskReads << std::endl;
    std::cout << "  Disk Writes: " << diskWrites << std::endl;
}

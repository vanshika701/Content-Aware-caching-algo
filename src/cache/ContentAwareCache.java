// ContentAwareCache.java
package cache;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigDecimal;
import java.math.MathContext;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.FileTime;
import java.time.Duration;
import java.time.Instant;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Map;

// Main cache manager class. All state is guarded by the object's monitor.
public class ContentAwareCache implements AutoCloseable {
    // Maximum cache size in bytes
    long maxCacheSize;
    // Current cache size in bytes
    long currentCacheSize = 0;

    // Cache storage: map from file path to cache entry
    private final Map<String, CacheEntry> cacheMap = new HashMap<>();

    // LRU order for basic eviction policy backup (first = least recently used)
    private final LinkedHashSet<String> lruList = new LinkedHashSet<>();

    // Statistics
    long cacheHits = 0;
    long cacheMisses = 0;
    long diskReads = 0;
    long diskWrites = 0;

    // File type priority weights (configurable)
    private final Map<String, Float> fileTypePriorities = new HashMap<>();

    public ContentAwareCache() {
        this(64L * 1024 * 1024);  // Default 64MB cache
    }

    public ContentAwareCache(long maxSize) {
        this.maxCacheSize = maxSize;

        // Setup default file type priorities
        fileTypePriorities.put(".txt", 0.7f);
        fileTypePriorities.put(".cfg", 0.9f);
        fileTypePriorities.put(".conf", 0.9f);
        fileTypePriorities.put(".ini", 0.9f);
        fileTypePriorities.put(".log", 0.6f);
        fileTypePriorities.put(".json", 0.8f);
        fileTypePriorities.put(".xml", 0.8f);
        fileTypePriorities.put(".cpp", 0.7f);
        fileTypePriorities.put(".h", 0.7f);
        fileTypePriorities.put(".c", 0.7f);
        fileTypePriorities.put(".py", 0.7f);
        fileTypePriorities.put(".jpg", 0.4f);
        fileTypePriorities.put(".png", 0.4f);
        fileTypePriorities.put(".pdf", 0.3f);
        fileTypePriorities.put(".exe", 0.1f);
        fileTypePriorities.put(".so", 0.1f);
        fileTypePriorities.put(".dll", 0.1f);
    }

    // Replaces the C++ destructor
    @Override
    public void close() {
        flush();
    }

    // Same semantics as std::filesystem::path::extension()
    public static String extensionOf(String filePath) {
        Path name = Paths.get(filePath).getFileName();
        if (name == null) {
            return "";
        }
        String fileName = name.toString();
        int dot = fileName.lastIndexOf('.');
        if (dot <= 0 || fileName.equals("..")) {
            return "";
        }
        return fileName.substring(dot);
    }

    private FileMetadata getFileMetadata(String filePath) {
        FileMetadata metadata = new FileMetadata();
        metadata.filePath = filePath;

        try {
            Path path = Paths.get(filePath);
            metadata.fileType = extensionOf(filePath);
            metadata.fileSize = Files.size(path);
            metadata.lastModified = Files.getLastModifiedTime(path);
        } catch (IOException e) {
            System.err.println("Error reading file metadata: " + e);
            metadata.fileSize = 0;
            metadata.lastModified = FileTime.from(Instant.now());
        }

        return metadata;
    }

    private float calculatePriorityScore(CacheEntry entry) {
        // Higher score = higher priority to keep in cache

        // Factor 1: File type priority (0.0-1.0)
        float typePriority = fileTypePriorities.getOrDefault(entry.metadata.fileType, 0.5f);

        // Factor 2: File size (favor smaller files)
        // 1.0 for files < 1KB, decreasing for larger files
        float sizeScore = 1.0f;
        if (entry.metadata.fileSize > 1024) {
            sizeScore = Math.min(1.0f, 10240.0f / (float) entry.metadata.fileSize);
        }

        // Factor 3: Access frequency
        // Log scale: more accesses = higher score
        float log2 = (float) (Math.log(1.0 + entry.stats.accessCount) / Math.log(2.0));
        float accessScore = 0.1f + Math.min(0.9f, log2 / 10.0f);

        // Factor 4: Recency of access
        // Score decreases as time since last access increases
        long duration = Duration.between(entry.stats.lastAccessed, Instant.now()).getSeconds();
        float recencyScore = (float) Math.exp(-duration / 3600.0f); // Decay over ~1 hour

        // Combine factors with weights
        return (typePriority * 0.3f) + (sizeScore * 0.2f)
                + (accessScore * 0.3f) + (recencyScore * 0.2f);
    }

    private void updateLRU(String filePath) {
        // Move to most-recently-used end
        lruList.remove(filePath);
        lruList.add(filePath);
    }

    private String findEntryForEviction() {
        if (cacheMap.isEmpty()) {
            return "";
        }

        // Find entry with lowest priority score
        String candidatePath = "";
        float lowestScore = Float.MAX_VALUE;

        for (Map.Entry<String, CacheEntry> pair : cacheMap.entrySet()) {
            if (pair.getValue().priorityScore < lowestScore) {
                lowestScore = pair.getValue().priorityScore;
                candidatePath = pair.getKey();
            }
        }

        // Fallback to LRU if no candidate was found
        if (candidatePath.isEmpty() && !lruList.isEmpty()) {
            candidatePath = lruList.iterator().next();
        }

        return candidatePath;
    }

    private boolean loadFileIntoCache(String filePath) {
        FileMetadata metadata = getFileMetadata(filePath);
        if (metadata.fileSize == 0) {
            return false;
        }

        // Make room in cache if needed
        makeRoomInCache(metadata.fileSize);

        // Read file into memory
        CacheEntry entry = new CacheEntry(metadata);
        try (InputStream in = Files.newInputStream(Paths.get(filePath))) {
            entry.data = in.readNBytes((int) metadata.fileSize);
        } catch (IOException e) {
            return false;
        }

        // Update cache
        cacheMap.put(filePath, entry);
        currentCacheSize += metadata.fileSize;
        updateLRU(filePath);
        diskReads++;

        // Calculate initial score
        entry.priorityScore = calculatePriorityScore(entry);

        return true;
    }

    private void evictFile(String filePath) {
        CacheEntry entry = cacheMap.remove(filePath);
        if (entry == null) {
            return;
        }

        // Update cache size
        currentCacheSize -= entry.getMemoryUsage();

        // Remove from LRU
        lruList.remove(filePath);
    }

    void makeRoomInCache(long requiredSize) {
        // Quick return if we have enough space
        if (currentCacheSize + requiredSize <= maxCacheSize) {
            return;
        }

        // Update all scores before eviction
        updateAllScores();

        // Evict files until we have enough space
        while (currentCacheSize + requiredSize > maxCacheSize && !cacheMap.isEmpty()) {
            String victimPath = findEntryForEviction();
            if (victimPath.isEmpty()) {
                break;
            }
            evictFile(victimPath);
        }

        // If still not enough space, increase max cache size
        if (currentCacheSize + requiredSize > maxCacheSize) {
            maxCacheSize = currentCacheSize + requiredSize;
        }
    }

    void updateEntryScore(String filePath) {
        CacheEntry entry = cacheMap.get(filePath);
        if (entry != null) {
            entry.priorityScore = calculatePriorityScore(entry);
        }
    }

    private void updateAllScores() {
        for (CacheEntry entry : cacheMap.values()) {
            entry.priorityScore = calculatePriorityScore(entry);
        }
    }

    // Returns null if the file cannot be opened
    public synchronized CacheFile openFile(String filePath, String mode) {
        // Check if file is already in cache
        CacheEntry cached = cacheMap.get(filePath);
        if (cached != null) {
            cacheHits++;
            updateLRU(filePath);
            return new CacheFile(cached, mode, this);
        }

        // File not in cache
        cacheMisses++;

        // Check if file exists for reading
        if (mode.indexOf('r') >= 0 && !Files.exists(Paths.get(filePath))) {
            return null;
        }

        // Create empty file for writing
        if (mode.indexOf('w') >= 0) {
            FileMetadata metadata = getFileMetadata(filePath);
            metadata.fileSize = 0; // Start with empty file

            CacheEntry entry = new CacheEntry(metadata);
            cacheMap.put(filePath, entry);
            updateLRU(filePath);

            return new CacheFile(entry, mode, this);
        }

        // Load existing file for reading or appending
        if (loadFileIntoCache(filePath)) {
            return new CacheFile(cacheMap.get(filePath), mode, this);
        }

        return null;
    }

    public boolean closeFile(CacheFile file) {
        if (file != null) {
            file.close();
            return true;
        }
        return false;
    }

    public synchronized void flush() {
        for (CacheEntry entry : cacheMap.values()) {
            try (OutputStream out = Files.newOutputStream(Paths.get(entry.metadata.filePath))) {
                out.write(entry.data);
                diskWrites++;
            } catch (IOException e) {
                // Skip files that cannot be written
            }
        }
    }

    public synchronized void clear() {
        flush();  // Write all changes to disk first

        cacheMap.clear();
        lruList.clear();
        currentCacheSize = 0;
    }

    public synchronized void resizeCache(long newMaxSize) {
        if (newMaxSize < maxCacheSize) {
            // Need to evict some files
            makeRoomInCache(maxCacheSize - newMaxSize);
        }

        maxCacheSize = newMaxSize;
    }

    public synchronized void setFileTypePriority(String extension, float priority) {
        // Ensure extension starts with a dot
        String ext = extension;
        if (!ext.isEmpty() && ext.charAt(0) != '.') {
            ext = "." + ext;
        }

        fileTypePriorities.put(ext, Math.max(0.0f, Math.min(1.0f, priority)));

        // Update scores for files of this type
        for (CacheEntry entry : cacheMap.values()) {
            if (ext.equals(entry.metadata.fileType)) {
                entry.priorityScore = calculatePriorityScore(entry);
            }
        }
    }

    public synchronized float getHitRate() {
        long totalAccesses = cacheHits + cacheMisses;
        if (totalAccesses == 0) {
            return 0.0f;
        }
        return (float) cacheHits / (float) totalAccesses;
    }

    public synchronized long getDiskReadCount() { return diskReads; }
    public synchronized long getDiskWriteCount() { return diskWrites; }
    public synchronized long getCacheHits() { return cacheHits; }
    public synchronized long getCacheMisses() { return cacheMisses; }

    // For testing
    public synchronized long getCacheSize() { return currentCacheSize; }
    public synchronized long getMaxCacheSize() { return maxCacheSize; }
    public synchronized int getCacheEntryCount() { return cacheMap.size(); }

    public synchronized void printStats() {
        System.out.println("Cache Statistics:");
        System.out.println("  Cache Size: " + currentCacheSize + " / " + maxCacheSize + " bytes");
        System.out.println("  Cache Entries: " + cacheMap.size());
        System.out.println("  Cache Hits: " + cacheHits);
        System.out.println("  Cache Misses: " + cacheMisses);
        System.out.println("  Hit Rate: " + formatFloat(getHitRate() * 100.0f) + "%");
        System.out.println("  Disk Reads: " + diskReads);
        System.out.println("  Disk Writes: " + diskWrites);
    }

    // Formats like C++ iostream's default float output (6 significant digits)
    public static String formatFloat(float value) {
        if (value == 0.0f) {
            return "0";
        }
        return new BigDecimal(value).round(new MathContext(6)).stripTrailingZeros().toPlainString();
    }
}

// TestCache.java - performance comparison of LRU vs content-aware caching
//
// Usage: java -cp out cache.TestCache [seed]
// Passing a seed makes the generated files and workloads reproducible.
package cache;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.stream.Stream;

public class TestCache {

    // Utility Functions
    static void createTestFile(String filePath, int size, byte fillChar) {
        byte[] buffer = new byte[size];
        Arrays.fill(buffer, fillChar);
        try {
            Files.write(Paths.get(filePath), buffer);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    static void createTestDirectory(String dirPath) {
        try {
            Files.createDirectories(Paths.get(dirPath));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    static void cleanTestDirectory(String dirPath) {
        Path dir = Paths.get(dirPath);
        if (!Files.exists(dir)) {
            return;
        }
        try (Stream<Path> walk = Files.walk(dir)) {
            walk.sorted(Comparator.reverseOrder()).forEach(p -> p.toFile().delete());
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    // Inclusive range, like std::uniform_int_distribution
    static int uniformInt(Random rng, int min, int max) {
        return min + rng.nextInt(max - min + 1);
    }

    // Enhanced Test Data Generator
    static class TestDataGenerator implements AutoCloseable {
        static class FileTypeInfo {
            final String extension;
            final int minSize;
            final int maxSize;
            final float importance;

            FileTypeInfo(String extension, int minSize, int maxSize, float importance) {
                this.extension = extension;
                this.minSize = minSize;
                this.maxSize = maxSize;
                this.importance = importance;
            }
        }

        private final Random rng;
        private final String testDir;
        private final List<FileTypeInfo> fileTypes;

        TestDataGenerator(String dir, Random rng) {
            this.rng = rng;
            this.testDir = dir;

            createTestDirectory(testDir);

            // Define various file types with different size ranges and importance
            fileTypes = List.of(
                new FileTypeInfo(".cfg", 1 * 1024, 10 * 1024, 0.9f),       // Small config files (high importance)
                new FileTypeInfo(".xml", 5 * 1024, 50 * 1024, 0.8f),       // Medium XML files (high importance)
                new FileTypeInfo(".json", 2 * 1024, 30 * 1024, 0.8f),      // Small-medium JSON files (high importance)
                new FileTypeInfo(".log", 100 * 1024, 500 * 1024, 0.6f),    // Larger log files (medium importance)
                new FileTypeInfo(".txt", 1 * 1024, 100 * 1024, 0.7f),      // Text files of various sizes (medium importance)
                new FileTypeInfo(".dat", 200 * 1024, 1024 * 1024, 0.4f),   // Large data files (lower importance)
                new FileTypeInfo(".bin", 500 * 1024, 2 * 1024 * 1024, 0.3f), // Large binary files (lower importance)
                new FileTypeInfo(".tmp", 10 * 1024, 100 * 1024, 0.2f)      // Temporary files (lowest importance)
            );
        }

        @Override
        public void close() {
            cleanTestDirectory(testDir);
        }

        String generateFile(int typeIndex, int fileIndex) {
            FileTypeInfo typeInfo = fileTypes.get(typeIndex % fileTypes.size());
            String filePath = testDir + "/file_" + fileIndex + typeInfo.extension;

            // Generate random size within the range for this file type
            int size = uniformInt(rng, typeInfo.minSize, typeInfo.maxSize);

            byte fillChar = (byte) uniformInt(rng, 'A', 'Z');
            createTestFile(filePath, size, fillChar);

            return filePath;
        }

        List<String> generateTestSet(int count) {
            List<String> files = new ArrayList<>();

            // Distribute files among different types
            for (int i = 0; i < count; i++) {
                files.add(generateFile(i % fileTypes.size(), i));
            }

            return files;
        }

        List<FileTypeInfo> getFileTypes() {
            return fileTypes;
        }
    }

    // Standard LRU Cache implementation for comparison
    static class LRUCache {
        private final long maxCacheSize;
        private long currentCacheSize = 0;
        // Access-ordered map: iteration starts at the least recently used entry
        private final LinkedHashMap<String, byte[]> cache = new LinkedHashMap<>(16, 0.75f, true);

        private long cacheHits = 0;
        private long cacheMisses = 0;
        private long diskReads = 0;

        LRUCache(long maxSize) {
            this.maxCacheSize = maxSize;
        }

        boolean accessFile(String filePath) {
            if (cache.get(filePath) != null) {
                // Cache hit (get() moved it to the most-recently-used position)
                cacheHits++;
                return true;
            }

            // Cache miss
            cacheMisses++;

            Path path = Paths.get(filePath);
            long fileSize;
            try {
                fileSize = Files.size(path);
            } catch (IOException e) {
                return false;
            }

            // Make room in cache if needed
            Iterator<Map.Entry<String, byte[]>> it = cache.entrySet().iterator();
            while (currentCacheSize + fileSize > maxCacheSize && it.hasNext()) {
                Map.Entry<String, byte[]> victim = it.next();
                currentCacheSize -= victim.getValue().length;
                it.remove();
            }

            // Add to cache if it fits
            if (fileSize <= maxCacheSize) {
                try {
                    byte[] data = Files.readAllBytes(path);
                    diskReads++;
                    cache.put(filePath, data);
                    currentCacheSize += fileSize;
                } catch (IOException e) {
                    return false;
                }
            } else {
                // File too large for cache, just read it
                diskReads++;
            }

            return true;
        }

        float getHitRate() {
            long totalAccesses = cacheHits + cacheMisses;
            if (totalAccesses == 0) {
                return 0.0f;
            }
            return (float) cacheHits / (float) totalAccesses;
        }

        long getDiskReadCount() { return diskReads; }
        long getCacheHits() { return cacheHits; }
        long getCacheMisses() { return cacheMisses; }
        long getCacheSize() { return currentCacheSize; }
        int getCacheEntryCount() { return cache.size(); }
    }

    // Enhanced workload generator with realistic patterns
    static class WorkloadGenerator {
        private final Random rng;
        private final List<String> files;
        private final List<String> fileTypes = new ArrayList<>(); // File extensions

        WorkloadGenerator(List<String> fileSet, Random rng) {
            this.rng = rng;
            this.files = fileSet;

            // Extract file types from paths
            for (String file : files) {
                fileTypes.add(ContentAwareCache.extensionOf(file));
            }
        }

        private String randomFile() {
            return files.get(rng.nextInt(files.size()));
        }

        // Generate workload with three phases to simulate real application behavior
        List<String> generateRealisticWorkload(int totalAccesses) {
            List<String> workload = new ArrayList<>(totalAccesses);

            // Phase 1 (30%): Application startup - config files loaded, initial resources accessed
            int startupPhase = (int) (totalAccesses * 0.3);

            // Config files accessed first
            for (int i = 0; i < files.size(); i++) {
                String ext = fileTypes.get(i);
                if (ext.equals(".cfg") || ext.equals(".json") || ext.equals(".xml")) {
                    // Access config files multiple times during startup
                    for (int j = 0; j < 5 && workload.size() < startupPhase; j++) {
                        workload.add(files.get(i));
                    }
                }
            }

            // Fill remaining startup phase with random accesses
            while (workload.size() < startupPhase) {
                workload.add(randomFile());
            }

            // Phase 2 (60%): Normal operation - locality with occasional bursts
            int operationPhase = (int) (totalAccesses * 0.6);
            int normalOpEnd = startupPhase + operationPhase;

            // Set up clusters of related file accesses
            int clusterSize = 5;
            while (workload.size() < normalOpEnd) {
                // Choose a random starting file
                int baseFile = rng.nextInt(files.size());

                // Create a cluster of accesses around this file and similar types
                for (int i = 0; i < clusterSize && workload.size() < normalOpEnd; i++) {
                    if (i % 2 == 0) {
                        // Sometimes access the base file
                        workload.add(files.get(baseFile));
                    } else {
                        // Find a file with the same extension
                        String targetExt = fileTypes.get(baseFile);
                        List<Integer> sameTypeFiles = new ArrayList<>();

                        for (int j = 0; j < files.size(); j++) {
                            if (fileTypes.get(j).equals(targetExt)) {
                                sameTypeFiles.add(j);
                            }
                        }

                        if (!sameTypeFiles.isEmpty()) {
                            workload.add(files.get(sameTypeFiles.get(rng.nextInt(sameTypeFiles.size()))));
                        } else {
                            workload.add(randomFile());
                        }
                    }
                }

                // Occasionally access some random files (context switch)
                if (rng.nextFloat() < 0.3f) {
                    for (int i = 0; i < 3 && workload.size() < normalOpEnd; i++) {
                        workload.add(randomFile());
                    }
                }
            }

            // Phase 3 (10%): Application wind-down - log files, cleanup
            while (workload.size() < totalAccesses) {
                // Higher probability of accessing log files
                boolean accessLog = rng.nextFloat() < 0.6f;

                if (accessLog) {
                    // Find and access a log file
                    List<Integer> logFiles = new ArrayList<>();
                    for (int i = 0; i < files.size(); i++) {
                        if (fileTypes.get(i).equals(".log")) {
                            logFiles.add(i);
                        }
                    }

                    if (!logFiles.isEmpty()) {
                        workload.add(files.get(logFiles.get(rng.nextInt(logFiles.size()))));
                    } else {
                        workload.add(randomFile());
                    }
                } else {
                    // Random access to any file
                    workload.add(randomFile());
                }
            }

            return workload;
        }

        // Specific pattern that heavily favors important files
        List<String> generateImportantFilesBurstWorkload(int totalAccesses) {
            List<String> workload = new ArrayList<>(totalAccesses);

            // Group files by extension
            Map<String, List<Integer>> filesByType = new HashMap<>();
            for (int i = 0; i < files.size(); i++) {
                filesByType.computeIfAbsent(fileTypes.get(i), k -> new ArrayList<>()).add(i);
            }

            // List of important extensions in order of importance
            List<String> importantExts = List.of(".cfg", ".json", ".xml", ".txt");

            int pos = 0;
            while (pos < totalAccesses) {
                // Random extension burst (70% of accesses)
                String burstExt = importantExts.get(rng.nextInt(importantExts.size()));
                List<Integer> burstFiles = filesByType.getOrDefault(burstExt, List.of());

                // If we have files of this type
                if (!burstFiles.isEmpty()) {
                    // Determine burst length (5-20 accesses)
                    int burstLength = Math.min(uniformInt(rng, 5, 20), totalAccesses - pos);

                    for (int i = 0; i < burstLength; i++) {
                        // Pick random file of this type
                        workload.add(files.get(burstFiles.get(rng.nextInt(burstFiles.size()))));
                        pos++;
                    }
                }

                // Random accesses (30% of total)
                int randomLength = Math.min(uniformInt(rng, 1, 5), totalAccesses - pos);

                for (int i = 0; i < randomLength; i++) {
                    workload.add(randomFile());
                    pos++;
                }
            }

            return workload;
        }
    }

    // Test function for standard caching
    static void testStandardCaching(List<String> workload, long cacheSize) {
        System.out.println("Testing standard LRU caching...");

        LRUCache lruCache = new LRUCache(cacheSize);

        long startTime = System.nanoTime();

        for (String filePath : workload) {
            lruCache.accessFile(filePath);
        }

        long durationMs = (System.nanoTime() - startTime) / 1_000_000;

        System.out.println("LRU Results:");
        System.out.println("  Cache Size: " + lruCache.getCacheSize() + " / " + cacheSize + " bytes");
        System.out.println("  Cache Entries: " + lruCache.getCacheEntryCount());
        System.out.println("  Cache Hits: " + lruCache.getCacheHits());
        System.out.println("  Cache Misses: " + lruCache.getCacheMisses());
        System.out.println("  Hit Rate: " + ContentAwareCache.formatFloat(lruCache.getHitRate() * 100.0f) + "%");
        System.out.println("  Disk Reads: " + lruCache.getDiskReadCount());
        System.out.println("  Execution Time: " + durationMs + "ms");
    }

    // Test function for content-aware caching
    static void testContentAwareCaching(List<String> workload, long cacheSize,
                                        List<TestDataGenerator.FileTypeInfo> fileTypes) {
        System.out.println("Testing content-aware caching...");

        try (ContentAwareCache cache = new ContentAwareCache(cacheSize)) {
            // Set file type priorities based on the file type information
            for (TestDataGenerator.FileTypeInfo type : fileTypes) {
                cache.setFileTypePriority(type.extension, type.importance);
            }

            long startTime = System.nanoTime();

            byte[] buffer = new byte[1024];
            for (String filePath : workload) {
                CacheFile file = cache.openFile(filePath, "r");
                if (file != null) {
                    // Read a small amount to simulate file access
                    file.read(buffer);
                    cache.closeFile(file);
                }
            }

            long durationMs = (System.nanoTime() - startTime) / 1_000_000;

            System.out.println("Content-Aware Results:");
            cache.printStats();
            System.out.println("  Execution Time: " + durationMs + "ms");
        }
    }

    public static void main(String[] args) {
        Random rng = args.length > 0 ? new Random(Long.parseLong(args[0])) : new Random();

        System.out.println("Content-Aware Caching Algorithm Test");
        System.out.println("=====================================");

        // Create test files with diverse types and sizes
        try (TestDataGenerator generator = new TestDataGenerator("./test_files", rng)) {
            List<String> testFiles = generator.generateTestSet(100);

            System.out.println("Created " + testFiles.size() + " test files.");

            // Create workload generator
            WorkloadGenerator workloadGen = new WorkloadGenerator(testFiles, rng);

            // Generate a realistic workload
            List<String> realisticWorkload = workloadGen.generateRealisticWorkload(20000);

            System.out.println("Generated realistic workload of " + realisticWorkload.size() + " file accesses.");

            // Set a smaller cache size to force eviction decisions
            // Use approximately 25% of what would be needed to cache all files
            long estimatedTotalSize = 0;
            for (String filePath : testFiles) {
                try {
                    estimatedTotalSize += Files.size(Paths.get(filePath));
                } catch (IOException e) {
                    // Ignore errors
                }
            }
            long cacheSize = estimatedTotalSize / 4;

            System.out.println("Using cache size of " + (cacheSize / 1024 / 1024) + " MB");
            System.out.println("(Approximately 25% of total data size)");

            // Test standard LRU caching
            testStandardCaching(realisticWorkload, cacheSize);

            System.out.println();

            // Test content-aware caching
            testContentAwareCaching(realisticWorkload, cacheSize, generator.getFileTypes());

            System.out.println("\n--- Additional Test: Important Files Burst Pattern ---\n");

            // Generate workload with bursts of important file accesses
            List<String> burstWorkload = workloadGen.generateImportantFilesBurstWorkload(10000);

            System.out.println("Generated important-files burst workload of " + burstWorkload.size() + " file accesses.");

            // Test standard LRU caching
            testStandardCaching(burstWorkload, cacheSize);

            System.out.println();

            // Test content-aware caching
            testContentAwareCaching(burstWorkload, cacheSize, generator.getFileTypes());
        }
    }
}

// CacheEntry.java
package cache;

// Cache entry representing a file in cache
public class CacheEntry {
    public final FileMetadata metadata;
    public final AccessStats stats = new AccessStats();
    public byte[] data = new byte[0];
    public float priorityScore = 0.0f;

    public CacheEntry(FileMetadata meta) {
        this.metadata = meta.copy();
    }

    public long getMemoryUsage() {
        return data.length;
    }
}

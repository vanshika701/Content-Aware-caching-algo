// AccessStats.java
package cache;

import java.time.Instant;

// Tracks file access statistics
public class AccessStats {
    public long accessCount = 0;
    public Instant lastAccessed = Instant.now();
}

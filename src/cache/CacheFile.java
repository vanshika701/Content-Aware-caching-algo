// CacheFile.java
package cache;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.Arrays;

// File handle for cached files (the Java counterpart of FILE*)
public class CacheFile implements AutoCloseable {
    public static final int SEEK_SET = 0;
    public static final int SEEK_CUR = 1;
    public static final int SEEK_END = 2;

    private final CacheEntry entry;
    private final String mode;
    private final ContentAwareCache cache;
    private int position = 0;
    private boolean modified = false;
    private boolean closed = false;

    CacheFile(CacheEntry entry, String mode, ContentAwareCache cache) {
        this.entry = entry;
        this.mode = mode;
        this.cache = cache;
    }

    // Replaces the C++ destructor: flush pending changes and update access stats
    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;

        // Flush changes if needed
        if (modified) {
            flush();
        }

        // Update access stats
        synchronized (cache) {
            entry.stats.accessCount++;
            entry.stats.lastAccessed = Instant.now();
            cache.updateEntryScore(entry.metadata.filePath);
        }
    }

    public int read(byte[] buffer) {
        return read(buffer, 0, buffer.length);
    }

    // Returns the number of bytes read (0 at end of file or if not opened for reading)
    public int read(byte[] buffer, int offset, int length) {
        if (mode.indexOf('r') < 0) {
            // Not opened for reading
            return 0;
        }

        int bytesAvailable = entry.data.length - position;
        int bytesToCopy = Math.min(length, bytesAvailable);

        if (bytesToCopy > 0) {
            System.arraycopy(entry.data, position, buffer, offset, bytesToCopy);
            position += bytesToCopy;
        }

        return bytesToCopy;
    }

    public int write(byte[] buffer) {
        return write(buffer, 0, buffer.length);
    }

    // Returns the number of bytes written (0 if not opened for writing)
    public int write(byte[] buffer, int offset, int length) {
        if (mode.indexOf('w') < 0 && mode.indexOf('a') < 0) {
            // Not opened for writing
            return 0;
        }

        // If appending, move to the end
        if (mode.indexOf('a') >= 0 && position != entry.data.length) {
            position = entry.data.length;
        }

        // Check if we need to resize the buffer
        if (position + length > entry.data.length) {
            // Request additional space from the cache
            int oldSize = entry.data.length;
            int newSize = position + length;
            int additionalSpace = newSize - oldSize;

            synchronized (cache) {
                cache.makeRoomInCache(additionalSpace);
                entry.data = Arrays.copyOf(entry.data, newSize);
                cache.currentCacheSize += additionalSpace;
            }
        }

        // Copy the data
        System.arraycopy(buffer, offset, entry.data, position, length);
        position += length;
        modified = true;

        return length;
    }

    public int seek(long offset, int origin) {
        long newPosition;

        switch (origin) {
            case SEEK_SET:
                newPosition = offset;
                break;
            case SEEK_CUR:
                newPosition = position + offset;
                break;
            case SEEK_END:
                newPosition = entry.data.length + offset;
                break;
            default:
                return -1;
        }

        if (newPosition < 0 || newPosition > entry.data.length) {
            // Cannot seek outside the file
            return -1;
        }

        position = (int) newPosition;
        return 0;
    }

    public long tell() {
        return position;
    }

    public int flush() {
        if (!modified) {
            return 0;
        }

        // Write back to disk
        try (OutputStream out = Files.newOutputStream(Paths.get(entry.metadata.filePath))) {
            out.write(entry.data);
        } catch (IOException e) {
            return -1;
        }

        synchronized (cache) {
            cache.diskWrites++;
        }

        modified = false;
        return 0;
    }
}

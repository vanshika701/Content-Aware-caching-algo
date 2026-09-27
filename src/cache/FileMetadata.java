// FileMetadata.java
package cache;

import java.nio.file.attribute.FileTime;

// Stores file metadata
public class FileMetadata {
    public String filePath;
    public String fileType;
    public long fileSize;
    public FileTime lastModified;

    public FileMetadata copy() {
        FileMetadata m = new FileMetadata();
        m.filePath = filePath;
        m.fileType = fileType;
        m.fileSize = fileSize;
        m.lastModified = lastModified;
        return m;
    }
}

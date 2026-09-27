// Main.java - interactive command-line interface
package cache;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class Main {

    private static void displayHelp() {
        System.out.println("Content-Aware Caching System");
        System.out.println("===========================");
        System.out.println();
        System.out.println("Usage:");
        System.out.println("  read <filename>                - Read a file through cache");
        System.out.println("  write <filename> <content>     - Write content to a file through cache");
        System.out.println("  append <filename> <content>    - Append content to a file through cache");
        System.out.println("  flush                          - Flush all changes to disk");
        System.out.println("  clear                          - Clear the cache");
        System.out.println("  stats                          - Show cache statistics");
        System.out.println("  resize <size_mb>               - Resize the cache (in MB)");
        System.out.println("  priority <ext> <value>         - Set priority for file type (0.0-1.0)");
        System.out.println("  run <filename>                 - Run the test suite");
        System.out.println("  help                           - Show this help");
        System.out.println("  exit                           - Exit the program");
    }

    private static void readFile(ContentAwareCache cache, String filename) {
        long startTime = System.nanoTime();

        CacheFile file = cache.openFile(filename, "r");
        if (file == null) {
            System.out.println("Error: Could not open file '" + filename + "' for reading.");
            return;
        }

        // Read the file
        byte[] buffer = new byte[4096];
        ByteArrayOutputStream content = new ByteArrayOutputStream();

        int bytesRead;
        while ((bytesRead = file.read(buffer)) > 0) {
            content.write(buffer, 0, bytesRead);
        }

        cache.closeFile(file);

        long durationUs = (System.nanoTime() - startTime) / 1000;

        // Display content and stats
        byte[] bytes = content.toByteArray();
        System.out.println("File content (" + bytes.length + " bytes):");
        if (bytes.length > 1024) {
            // Display just the beginning and end
            System.out.println(new String(bytes, 0, 512, StandardCharsets.UTF_8) + "...");
            System.out.println("..." + new String(bytes, bytes.length - 512, 512, StandardCharsets.UTF_8));
        } else {
            System.out.println(new String(bytes, StandardCharsets.UTF_8));
        }

        System.out.println("Read operation completed in " + durationUs + " microseconds.");
    }

    private static void writeFile(ContentAwareCache cache, String filename, String content, boolean append) {
        long startTime = System.nanoTime();
        String action = append ? "appending" : "writing";

        CacheFile file = cache.openFile(filename, append ? "a+" : "w");
        if (file == null) {
            System.out.println("Error: Could not open file '" + filename + "' for " + action + ".");
            return;
        }

        // Write the content
        int bytesWritten = file.write(content.getBytes(StandardCharsets.UTF_8));

        cache.closeFile(file);

        long durationUs = (System.nanoTime() - startTime) / 1000;

        if (append) {
            System.out.println("Appended " + bytesWritten + " bytes to '" + filename + "'.");
            System.out.println("Append operation completed in " + durationUs + " microseconds.");
        } else {
            System.out.println("Wrote " + bytesWritten + " bytes to '" + filename + "'.");
            System.out.println("Write operation completed in " + durationUs + " microseconds.");
        }
    }

    public static void main(String[] argv) throws IOException {
        // Create the cache with 64MB default size
        ContentAwareCache cache = new ContentAwareCache(64L * 1024 * 1024);

        System.out.println("Content-Aware Caching System");
        System.out.println("===========================");
        System.out.println("Type 'help' for a list of commands.");

        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));

        while (true) {
            System.out.print("> ");
            System.out.flush();
            String command = in.readLine();
            if (command == null) {
                break;  // End of input
            }

            // Parse command and arguments
            String[] args = command.split(" ", -1);

            switch (args[0]) {
                case "":
                    break;
                case "help":
                    displayHelp();
                    break;
                case "exit":
                    cache.flush();
                    System.out.println("Exiting. All changes have been saved.");
                    return;
                case "read":
                    if (args.length < 2) {
                        System.out.println("Error: Missing filename.");
                        break;
                    }
                    readFile(cache, args[1]);
                    break;
                case "write":
                case "append":
                    if (args.length < 3) {
                        System.out.println("Error: Missing filename or content.");
                        break;
                    }
                    // Combine remaining args as content
                    String content = String.join(" ", Arrays.copyOfRange(args, 2, args.length));
                    writeFile(cache, args[1], content, args[0].equals("append"));
                    break;
                case "flush":
                    cache.flush();
                    System.out.println("Cache flushed to disk.");
                    break;
                case "clear":
                    cache.clear();
                    System.out.println("Cache cleared.");
                    break;
                case "stats":
                    cache.printStats();
                    break;
                case "resize":
                    if (args.length < 2) {
                        System.out.println("Error: Missing size parameter.");
                        break;
                    }
                    try {
                        float sizeMB = Float.parseFloat(args[1]);
                        long sizeBytes = (long) (sizeMB * 1024 * 1024);
                        cache.resizeCache(sizeBytes);
                        System.out.println("Cache resized to " + ContentAwareCache.formatFloat(sizeMB) + " MB.");
                    } catch (NumberFormatException e) {
                        System.out.println("Error: Invalid size parameter.");
                    }
                    break;
                case "priority":
                    if (args.length < 3) {
                        System.out.println("Error: Missing extension or priority value.");
                        break;
                    }
                    try {
                        float priority = Float.parseFloat(args[2]);
                        cache.setFileTypePriority(args[1], priority);
                        System.out.println("Set priority of " + args[1] + " files to "
                                + ContentAwareCache.formatFloat(priority) + ".");
                    } catch (NumberFormatException e) {
                        System.out.println("Error: Invalid priority value.");
                    }
                    break;
                case "run":
                    if (args.length < 2) {
                        System.out.println("Error: Missing test filename.");
                        break;
                    }
                    System.out.println("Running tests from '" + args[1] + "'...");
                    // This would typically load and run a test suite
                    System.out.println("Test runner not implemented in this demo.");
                    break;
                default:
                    System.out.println("Unknown command: " + args[0]);
                    System.out.println("Type 'help' for a list of commands.");
            }
        }

        // Ensure everything is flushed before exiting
        cache.flush();
        System.out.println("Exiting. All changes have been saved.");
    }
}

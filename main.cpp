// main.cpp
#include "content_aware_cache.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

void displayHelp() {
    std::cout << "Content-Aware Caching System" << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  read <filename>                - Read a file through cache" << std::endl;
    std::cout << "  write <filename> <content>     - Write content to a file through cache" << std::endl;
    std::cout << "  append <filename> <content>    - Append content to a file through cache" << std::endl;
    std::cout << "  flush                          - Flush all changes to disk" << std::endl;
    std::cout << "  clear                          - Clear the cache" << std::endl;
    std::cout << "  stats                          - Show cache statistics" << std::endl;
    std::cout << "  resize <size_mb>               - Resize the cache (in MB)" << std::endl;
    std::cout << "  priority <ext> <value>         - Set priority for file type (0.0-1.0)" << std::endl;
    std::cout << "  run <filename>                 - Run the test suite" << std::endl;
    std::cout << "  help                           - Show this help" << std::endl;
    std::cout << "  exit                           - Exit the program" << std::endl;
}

void readFile(std::shared_ptr<ContentAwareCache> cache, const std::string& filename) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    CacheFile* file = cache->openFile(filename, "r");
    if (!file) {
        std::cout << "Error: Could not open file '" << filename << "' for reading." << std::endl;
        return;
    }
    
    // Read the file
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    std::vector<char> content;
    
    size_t bytesRead;
    while ((bytesRead = file->read(buffer, 1, bufferSize)) > 0) {
        content.insert(content.end(), buffer, buffer + bytesRead);
    }
    
    cache->closeFile(file);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    // Display content and stats
    std::cout << "File content (" << content.size() << " bytes):" << std::endl;
    if (content.size() > 1024) {
        // Display just the beginning and end
        std::cout << std::string(content.begin(), content.begin() + 512) << "..." << std::endl;
        std::cout << "..." << std::string(content.end() - 512, content.end()) << std::endl;
    } else {
        std::cout << std::string(content.begin(), content.end()) << std::endl;
    }
    
    std::cout << "Read operation completed in " << duration.count() << " microseconds." << std::endl;
}

void writeFile(std::shared_ptr<ContentAwareCache> cache, const std::string& filename, const std::string& content) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    CacheFile* file = cache->openFile(filename, "w");
    if (!file) {
        std::cout << "Error: Could not open file '" << filename << "' for writing." << std::endl;
        return;
    }
    
    // Write the content
    size_t bytesWritten = file->write(content.c_str(), 1, content.size());
    
    cache->closeFile(file);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    std::cout << "Wrote " << bytesWritten << " bytes to '" << filename << "'." << std::endl;
    std::cout << "Write operation completed in " << duration.count() << " microseconds." << std::endl;
}

void appendFile(std::shared_ptr<ContentAwareCache> cache, const std::string& filename, const std::string& content) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    CacheFile* file = cache->openFile(filename, "a+");
    if (!file) {
        std::cout << "Error: Could not open file '" << filename << "' for appending." << std::endl;
        return;
    }
    
    // Write the content
    size_t bytesWritten = file->write(content.c_str(), 1, content.size());
    
    cache->closeFile(file);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    std::cout << "Appended " << bytesWritten << " bytes to '" << filename << "'." << std::endl;
    std::cout << "Append operation completed in " << duration.count() << " microseconds." << std::endl;
}

int main() {
    // Create the cache with 64MB default size
    auto cache = std::make_shared<ContentAwareCache>(64 * 1024 * 1024);
    
    std::cout << "Content-Aware Caching System" << std::endl;
    std::cout << "===========================" << std::endl;
    std::cout << "Type 'help' for a list of commands." << std::endl;
    
    std::string command;
    std::vector<std::string> args;
    
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, command);
        
        // Parse command and arguments
        args.clear();
        size_t pos = 0;
        size_t end;
        while ((end = command.find(" ", pos)) != std::string::npos) {
            args.push_back(command.substr(pos, end - pos));
            pos = end + 1;
        }
        args.push_back(command.substr(pos));
        
        if (args.empty()) {
            continue;
        }
        
        // Process commands
        if (args[0] == "help") {
            displayHelp();
        }
        else if (args[0] == "exit") {
            break;
        }
        else if (args[0] == "read") {
            if (args.size() < 2) {
                std::cout << "Error: Missing filename." << std::endl;
                continue;
            }
            readFile(cache, args[1]);
        }
        else if (args[0] == "write") {
            if (args.size() < 3) {
                std::cout << "Error: Missing filename or content." << std::endl;
                continue;
            }
            // Combine remaining args as content
            std::string content;
            for (size_t i = 2; i < args.size(); i++) {
                if (i > 2) content += " ";
                content += args[i];
            }
            writeFile(cache, args[1], content);
        }
        else if (args[0] == "append") {
            if (args.size() < 3) {
                std::cout << "Error: Missing filename or content." << std::endl;
                continue;
            }
            // Combine remaining args as content
            std::string content;
            for (size_t i = 2; i < args.size(); i++) {
                if (i > 2) content += " ";
                content += args[i];
            }
            appendFile(cache, args[1], content);
        }
        else if (args[0] == "flush") {
            cache->flush();
            std::cout << "Cache flushed to disk." << std::endl;
        }
        else if (args[0] == "clear") {
            cache->clear();
            std::cout << "Cache cleared." << std::endl;
        }
        else if (args[0] == "stats") {
            cache->printStats();
        }
        else if (args[0] == "resize") {
            if (args.size() < 2) {
                std::cout << "Error: Missing size parameter." << std::endl;
                continue;
            }
            try {
                float sizeMB = std::stof(args[1]);
                size_t sizeBytes = static_cast<size_t>(sizeMB * 1024 * 1024);
                cache->resizeCache(sizeBytes);
                std::cout << "Cache resized to " << sizeMB << " MB." << std::endl;
            }
            catch (const std::exception& e) {
                std::cout << "Error: Invalid size parameter." << std::endl;
            }
        }
        else if (args[0] == "priority") {
            if (args.size() < 3) {
                std::cout << "Error: Missing extension or priority value." << std::endl;
                continue;
            }
            try {
                float priority = std::stof(args[2]);
                cache->setFileTypePriority(args[1], priority);
                std::cout << "Set priority of " << args[1] << " files to " << priority << "." << std::endl;
            }
            catch (const std::exception& e) {
                std::cout << "Error: Invalid priority value." << std::endl;
            }
        }
        else if (args[0] == "run") {
            if (args.size() < 2) {
                std::cout << "Error: Missing test filename." << std::endl;
                continue;
            }
            
            std::cout << "Running tests from '" << args[1] << "'..." << std::endl;
            // This would typically load and run a test suite
            std::cout << "Test runner not implemented in this demo." << std::endl;
        }
        else {
            std::cout << "Unknown command: " << args[0] << std::endl;
            std::cout << "Type 'help' for a list of commands." << std::endl;
        }
    }
    
    // Ensure everything is flushed before exiting
    cache->flush();
    std::cout << "Exiting. All changes have been saved." << std::endl;
    
    return 0;
}

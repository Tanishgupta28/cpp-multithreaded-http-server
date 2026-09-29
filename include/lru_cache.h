#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct CachedFile {
    std::string body;
    std::string content_type;
};

class LRUCache {
public:
    explicit LRUCache(std::size_t capacity);
    std::shared_ptr<const CachedFile> get(const std::string& key);
    void put(const std::string& key, std::shared_ptr<const CachedFile> file);

private:
    struct Entry {
        std::string key;
        std::shared_ptr<const CachedFile> file;
    };
    const std::size_t capacity_;
    std::list<Entry> entries_; // Front is most recent; back is least recent.
    std::unordered_map<std::string, std::list<Entry>::iterator> lookup_;
    std::mutex mutex_;
};

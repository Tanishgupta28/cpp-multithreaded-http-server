#include "lru_cache.h"
#include <utility>

LRUCache::LRUCache(std::size_t capacity) : capacity_(capacity) {}

std::shared_ptr<const CachedFile> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = lookup_.find(key);
    if (found == lookup_.end()) return nullptr;
    entries_.splice(entries_.begin(), entries_, found->second);
    // Shared immutable ownership keeps the bytes alive after unlocking/eviction.
    return found->second->file;
}

void LRUCache::put(const std::string& key, std::shared_ptr<const CachedFile> file) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capacity_ == 0 || !file) return;
    const auto found = lookup_.find(key);
    if (found != lookup_.end()) {
        found->second->file = std::move(file);
        entries_.splice(entries_.begin(), entries_, found->second);
        return;
    }
    entries_.push_front({key, std::move(file)});
    try {
        lookup_.emplace(key, entries_.begin());
    } catch (...) {
        // Preserve the list/map invariant if allocation fails.
        entries_.pop_front();
        throw;
    }
    if (lookup_.size() > capacity_) {
        lookup_.erase(entries_.back().key);
        entries_.pop_back();
    }
}

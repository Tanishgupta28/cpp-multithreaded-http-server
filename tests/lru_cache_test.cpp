#include "lru_cache.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

void require(bool condition) {
    if (!condition) throw std::runtime_error("LRU cache check failed");
}

int main() {
    const auto a = std::make_shared<const CachedFile>(CachedFile{
        std::string("a\0b", 3), "application/octet-stream"});
    const auto b = std::make_shared<const CachedFile>(CachedFile{"B", "text/plain"});
    LRUCache cache(3);
    require(!cache.get("A"));
    cache.put("A", a);
    cache.put("B", b);
    cache.put("C", b);
    auto retained = cache.get("A"); // C, B, A becomes A, C, B (MRU first).
    require(retained && retained->body == std::string("a\0b", 3));
    require(retained->content_type == "application/octet-stream");
    cache.put("D", b);
    require(!cache.get("B"));
    require(cache.get("C") && cache.get("D") && cache.get("A"));
    for (int i = 0; i < 10; ++i) require(cache.get("A") == a);
    cache.put("C", a); // Updating an existing key also promotes it.
    cache.put("E", b);
    require(!cache.get("D"));
    require(cache.get("C") == a);
    for (int i = 0; i < 20; ++i) cache.put(std::to_string(i), b);
    require(!cache.get("16"));
    require(cache.get("17") && cache.get("18") && cache.get("19"));
    require(retained->body == std::string("a\0b", 3)); // Safe after eviction.
    LRUCache disabled(0);
    disabled.put("A", a);
    require(!disabled.get("A"));
    LRUCache one(1);
    one.put("A", a);
    one.put("B", b);
    require(!one.get("A") && one.get("B") == b);

    LRUCache shared(3);
    std::atomic<bool> valid{true};
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread) {
        workers.emplace_back([&shared, &valid, thread] {
            for (int i = 0; i < 1000; ++i) {
                const std::string key = std::to_string((i + thread) % 8);
                shared.put(key, std::make_shared<const CachedFile>(CachedFile{key, "text/plain"}));
                if (auto value = shared.get(key)) {
                    if (value->body != key || value->content_type != "text/plain") valid = false;
                } // A competing insertion may legitimately evict this key.
            }
        });
    }
    for (auto& worker : workers) worker.join();
    require(valid);
    int remaining = 0;
    for (int i = 0; i < 8; ++i) if (shared.get(std::to_string(i))) ++remaining;
    require(remaining == 3);
    std::cout << "PASS: miss/hit, promotion/update, eviction/capacity, binary/MIME, concurrent access\n";
}

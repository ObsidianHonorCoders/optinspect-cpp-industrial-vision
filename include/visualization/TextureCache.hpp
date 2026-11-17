#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace optinspect::viz {

/// Counters describing the behaviour of a cache; used by the performance panel and tests.
struct CacheStats {
    std::size_t hits = 0;
    std::size_t misses = 0;
    std::size_t evictions = 0;
    std::size_t replacements = 0;
    std::size_t invalidations = 0;
};

/// Bounded, least-recently-used cache of shared resources keyed by string.
///
/// Policy
///  - Key:          caller defined; the GUI uses "<absolute path>|<mtime>|<size>" so a
///                  file that changes on disk gets a new key (implicit invalidation).
///  - Budget:       the sum of the `bytes` given to put() is kept <= budget by evicting
///                  least-recently-used entries. The entry just inserted is never evicted,
///                  so a single resource larger than the budget is still usable.
///  - Ownership:    values are shared_ptr. Evicting or invalidating only drops the cache's
///                  reference; a texture still being drawn stays alive until its last
///                  user releases it.
///  - Threading:    NOT thread-safe. For GPU textures the cache must live on, and only be
///                  touched from, the thread owning the OpenGL context (it also
///                  destroys evicted textures).
///
/// The class does not depend on OpenGL, which keeps the policy unit-testable
/// with a dummy value type.
template <typename Value>
class BoundedLruCache {
public:
    explicit BoundedLruCache(std::size_t budget_bytes) : budget_(budget_bytes) {}

    /// Returns the cached value and marks it most recently used, or nullptr.
    std::shared_ptr<Value> get(const std::string& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            ++stats_.misses;
            return nullptr;
        }
        ++stats_.hits;
        order_.splice(order_.begin(), order_, it->second);
        return it->second->value;
    }

    /// Membership test that does not change recency or statistics.
    bool contains(const std::string& key) const { return index_.count(key) != 0; }

    /// Inserts or replaces an entry (most recently used) and evicts as needed.
    void put(const std::string& key, std::shared_ptr<Value> value, std::size_t bytes) {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            bytes_ -= it->second->bytes;
            order_.erase(it->second);
            index_.erase(it);
            ++stats_.replacements;
        }
        order_.push_front(Entry{key, std::move(value), bytes});
        index_[key] = order_.begin();
        bytes_ += bytes;
        evictToBudget();
    }

    bool invalidate(const std::string& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) return false;
        bytes_ -= it->second->bytes;
        order_.erase(it->second);
        index_.erase(it);
        ++stats_.invalidations;
        return true;
    }

    void clear() {
        stats_.invalidations += index_.size();
        order_.clear();
        index_.clear();
        bytes_ = 0;
    }

    void setBudget(std::size_t budget_bytes) {
        budget_ = budget_bytes;
        evictToBudget();
    }

    std::size_t budget() const { return budget_; }
    std::size_t bytes() const { return bytes_; }
    std::size_t size() const { return index_.size(); }
    const CacheStats& stats() const { return stats_; }

    /// Keys from most to least recently used.
    std::vector<std::string> keys() const {
        std::vector<std::string> out;
        for (const auto& e : order_) out.push_back(e.key);
        return out;
    }

private:
    struct Entry {
        std::string key;
        std::shared_ptr<Value> value;
        std::size_t bytes;
    };

    void evictToBudget() {
        while (bytes_ > budget_ && order_.size() > 1) {
            const Entry& victim = order_.back();
            bytes_ -= victim.bytes;
            index_.erase(victim.key);
            order_.pop_back();
            ++stats_.evictions;
        }
    }

    std::size_t budget_;
    std::size_t bytes_ = 0;
    std::list<Entry> order_;  ///< Front = most recently used.
    std::unordered_map<std::string, typename std::list<Entry>::iterator> index_;
    CacheStats stats_;
};

}  // namespace optinspect::viz

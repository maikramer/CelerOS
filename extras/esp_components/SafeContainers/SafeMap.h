/**
 * @file SafeMap.h
 * @brief Thread-safe map using FreeRTOS mutex
 * @author maikeu
 * @date 2024
 * 
 * Provides a thread-safe wrapper around std::map with RAII-based locking.
 */

#ifndef SAFE_MAP_H
#define SAFE_MAP_H

#include "LockableBase.h"
#include <map>
#include <functional>
#include <optional>
#include <vector>
#include <utility>

namespace SafeContainers {

/**
 * @class SafeMap
 * @brief Thread-safe map container based on std::map.
 * 
 * @tparam TKey The type of keys.
 * @tparam TValue The type of values.
 * 
 * All operations are protected by a mutex. For iteration, use withLock() or
 * the iteration guard.
 * 
 * Example:
 * @code
 * SafeMap<std::string, int> map;
 * map.insert("key1", 42);
 * 
 * if (auto value = map.get("key1")) {
 *     printf("Value: %d\n", *value);
 * }
 * 
 * // Safe iteration
 * map.forEach([](const std::string& key, const int& value) {
 *     printf("%s: %d\n", key.c_str(), value);
 * });
 * @endcode
 */
template<typename TKey, typename TValue>
class SafeMap : public LockableBase {
public:
    using key_type = TKey;
    using mapped_type = TValue;
    using value_type = std::pair<const TKey, TValue>;
    using size_type = typename std::map<TKey, TValue>::size_type;
    using iterator = typename std::map<TKey, TValue>::iterator;
    using const_iterator = typename std::map<TKey, TValue>::const_iterator;

    /**
     * @class IterationGuard
     * @brief RAII guard for safe iteration over the map.
     */
    class IterationGuard {
    public:
        IterationGuard(SafeMap& map, uint32_t timeoutMs)
            : _map(map), _lock(map.getMutex(), pdMS_TO_TICKS(timeoutMs)) {}

        ~IterationGuard() = default;

        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

        IterationGuard(IterationGuard&& other) noexcept
            : _map(other._map), _lock(std::move(other._lock)) {}

        [[nodiscard]] bool isLocked() const { return _lock.isLocked(); }
        explicit operator bool() const { return _lock.isLocked(); }

        [[nodiscard]] std::map<TKey, TValue>& get() { return _map._map; }
        [[nodiscard]] const std::map<TKey, TValue>& get() const { return _map._map; }

        iterator begin() { return _map._map.begin(); }
        iterator end() { return _map._map.end(); }
        const_iterator begin() const { return _map._map.begin(); }
        const_iterator end() const { return _map._map.end(); }
        const_iterator cbegin() const { return _map._map.cbegin(); }
        const_iterator cend() const { return _map._map.cend(); }

    private:
        SafeMap& _map;
        ScopedLock _lock;
    };

    SafeMap() = default;
    ~SafeMap() override = default;

    // Move operations
    SafeMap(SafeMap&& other) noexcept : LockableBase(std::move(other)) {
        auto lockOther = other.lock();
        if (lockOther) {
            _map = std::move(other._map);
        }
    }

    SafeMap& operator=(SafeMap&& other) noexcept {
        if (this != &other) {
            auto lockThis = lock();
            auto lockOther = other.lock();
            if (lockThis && lockOther) {
                LockableBase::operator=(std::move(other));
                _map = std::move(other._map);
            }
        }
        return *this;
    }

    // ==================== Basic Operations ====================

    /**
     * @brief Checks if the map is empty.
     * @return true if empty.
     */
    [[nodiscard]] bool empty() const {
        auto guard = lock();
        return guard ? _map.empty() : true;
    }

    /**
     * @brief Returns the number of elements.
     * @return Size of the map, or 0 if lock fails.
     */
    [[nodiscard]] size_type size() const {
        auto guard = lock();
        return guard ? _map.size() : 0;
    }

    /**
     * @brief Clears all elements from the map.
     */
    void clear() {
        auto guard = lock();
        if (guard) {
            _map.clear();
        }
    }

    // ==================== Element Access ====================

    /**
     * @brief Checks if key exists.
     * @param key Key to search for.
     * @return true if key exists.
     */
    [[nodiscard]] bool contains(const TKey& key) const {
        auto guard = lock();
        if (guard) {
            return _map.find(key) != _map.end();
        }
        return false;
    }

    /**
     * @brief Gets value for key.
     * @param key Key to search for.
     * @return std::optional with value if found.
     */
    [[nodiscard]] std::optional<TValue> get(const TKey& key) const {
        auto guard = lock();
        if (guard) {
            auto it = _map.find(key);
            if (it != _map.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Gets value or default.
     * @param key Key to search for.
     * @param defaultValue Value to return if key not found.
     * @return Value for key or default.
     */
    [[nodiscard]] TValue getOrDefault(const TKey& key, const TValue& defaultValue) const {
        auto guard = lock();
        if (guard) {
            auto it = _map.find(key);
            if (it != _map.end()) {
                return it->second;
            }
        }
        return defaultValue;
    }

    /**
     * @brief Safe bracket operator returning optional.
     * @param key Key to access.
     * @return Pair of (success, value).
     */
    [[nodiscard]] std::pair<bool, TValue> operator[](const TKey& key) const {
        auto guard = lock();
        if (guard) {
            auto it = _map.find(key);
            if (it != _map.end()) {
                return {true, it->second};
            }
        }
        return {false, TValue{}};
    }

    // ==================== Modifiers ====================

    /**
     * @brief Inserts or updates a key-value pair.
     * @param key Key to insert.
     * @param value Value to insert.
     * @return true if successful.
     */
    bool insert(const TKey& key, const TValue& value) {
        auto guard = lock();
        if (guard) {
            _map[key] = value;
            return true;
        }
        return false;
    }

    /**
     * @brief Inserts or updates (move version).
     */
    bool insert(const TKey& key, TValue&& value) {
        auto guard = lock();
        if (guard) {
            _map[key] = std::move(value);
            return true;
        }
        return false;
    }

    /**
     * @brief Inserts only if key doesn't exist.
     * @param key Key to insert.
     * @param value Value to insert.
     * @return true if inserted (key didn't exist).
     */
    bool insertIfAbsent(const TKey& key, const TValue& value) {
        auto guard = lock();
        if (guard) {
            if (_map.find(key) == _map.end()) {
                _map[key] = value;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Updates only if key exists.
     * @param key Key to update.
     * @param value New value.
     * @return true if updated (key existed).
     */
    bool update(const TKey& key, const TValue& value) {
        auto guard = lock();
        if (guard) {
            auto it = _map.find(key);
            if (it != _map.end()) {
                it->second = value;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Constructs element in-place.
     * @param key Key for the element.
     * @param args Arguments for value constructor.
     * @return true if successful.
     */
    template<typename... Args>
    bool emplace(const TKey& key, Args&&... args) {
        auto guard = lock();
        if (guard) {
            _map.emplace(key, TValue(std::forward<Args>(args)...));
            return true;
        }
        return false;
    }

    /**
     * @brief Removes element by key.
     * @param key Key to remove.
     * @return true if element was removed.
     */
    bool erase(const TKey& key) {
        auto guard = lock();
        if (guard) {
            return _map.erase(key) > 0;
        }
        return false;
    }

    /**
     * @brief Gets and removes value for key.
     * @param key Key to remove.
     * @return std::optional with removed value.
     */
    [[nodiscard]] std::optional<TValue> extract(const TKey& key) {
        auto guard = lock();
        if (guard) {
            auto it = _map.find(key);
            if (it != _map.end()) {
                TValue value = std::move(it->second);
                _map.erase(it);
                return value;
            }
        }
        return std::nullopt;
    }

    // ==================== Iteration ====================

    /**
     * @brief Executes function for each key-value pair.
     * @param func Function to apply.
     */
    void forEach(std::function<void(const TKey&, const TValue&)> func) const {
        auto guard = lock();
        if (guard) {
            for (const auto& [key, value] : _map) {
                func(key, value);
            }
        }
    }

    /**
     * @brief Executes function for each key-value pair (mutable value).
     * @param func Function to apply.
     */
    void forEachMut(std::function<void(const TKey&, TValue&)> func) {
        auto guard = lock();
        if (guard) {
            for (auto& [key, value] : _map) {
                func(key, value);
            }
        }
    }

    /**
     * @brief Gets an iteration guard for manual iteration.
     * @param timeoutMs Lock timeout.
     * @return IterationGuard - check with isLocked() before using.
     */
    [[nodiscard]] IterationGuard iterationGuard(uint32_t timeoutMs = 1000) {
        return IterationGuard(*this, timeoutMs);
    }

    /**
     * @brief Executes function with locked access to internal map.
     * @param func Function receiving reference to internal map.
     * @return true if function was executed.
     */
    bool withLock(std::function<void(std::map<TKey, TValue>&)> func) {
        auto guard = lock();
        if (guard) {
            func(_map);
            return true;
        }
        return false;
    }

    // ==================== Bulk Operations ====================

    /**
     * @brief Gets all keys.
     * @return Vector of all keys.
     */
    [[nodiscard]] std::vector<TKey> keys() const {
        auto guard = lock();
        std::vector<TKey> result;
        if (guard) {
            result.reserve(_map.size());
            for (const auto& [key, value] : _map) {
                result.push_back(key);
            }
        }
        return result;
    }

    /**
     * @brief Gets all values.
     * @return Vector of all values.
     */
    [[nodiscard]] std::vector<TValue> values() const {
        auto guard = lock();
        std::vector<TValue> result;
        if (guard) {
            result.reserve(_map.size());
            for (const auto& [key, value] : _map) {
                result.push_back(value);
            }
        }
        return result;
    }

    /**
     * @brief Converts to vector of pairs.
     * @return Vector of key-value pairs.
     */
    [[nodiscard]] std::vector<std::pair<TKey, TValue>> toVector() const {
        auto guard = lock();
        std::vector<std::pair<TKey, TValue>> result;
        if (guard) {
            result.reserve(_map.size());
            for (const auto& [key, value] : _map) {
                result.emplace_back(key, value);
            }
        }
        return result;
    }

private:
    std::map<TKey, TValue> _map;
};

} // namespace SafeContainers

#endif // SAFE_MAP_H

/**
 * @file SafeList.h
 * @brief Thread-safe doubly-linked list using FreeRTOS mutex
 * @author maikeu
 * @date 2024
 * 
 * Provides a thread-safe wrapper around std::list with RAII-based locking.
 */

#ifndef SAFE_LIST_H
#define SAFE_LIST_H

#include "LockableBase.h"
#include <list>
#include <functional>
#include <optional>
#include <algorithm>

namespace SafeContainers {

/**
 * @class SafeList
 * @brief Thread-safe list container based on std::list.
 * 
 * @tparam T The type of elements stored in the list.
 * 
 * All operations are protected by a mutex. For iteration, use withLock() or
 * the iteration guard to prevent data races.
 * 
 * Example:
 * @code
 * SafeList<int> list;
 * list.pushBack(42);
 * 
 * // Safe iteration
 * list.forEach([](const int& val) {
 *     printf("Value: %d\n", val);
 * });
 * 
 * // Manual iteration with guard
 * {
 *     auto guard = list.iterationGuard();
 *     if (guard) {
 *         for (const auto& item : guard.get()) {
 *             // Process item
 *         }
 *     }
 * }
 * @endcode
 */
template<typename T>
class SafeList : public LockableBase {
public:
    using value_type = T;
    using size_type = typename std::list<T>::size_type;
    using iterator = typename std::list<T>::iterator;
    using const_iterator = typename std::list<T>::const_iterator;

    /**
     * @class IterationGuard
     * @brief RAII guard for safe iteration over the list.
     */
    class IterationGuard {
    public:
        IterationGuard(SafeList& list, uint32_t timeoutMs)
            : _list(list), _lock(list.getMutex(), pdMS_TO_TICKS(timeoutMs)) {}

        ~IterationGuard() = default;

        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

        IterationGuard(IterationGuard&& other) noexcept
            : _list(other._list), _lock(std::move(other._lock)) {}

        [[nodiscard]] bool isLocked() const { return _lock.isLocked(); }
        explicit operator bool() const { return _lock.isLocked(); }

        [[nodiscard]] std::list<T>& get() { return _list._list; }
        [[nodiscard]] const std::list<T>& get() const { return _list._list; }

        iterator begin() { return _list._list.begin(); }
        iterator end() { return _list._list.end(); }
        const_iterator begin() const { return _list._list.begin(); }
        const_iterator end() const { return _list._list.end(); }
        const_iterator cbegin() const { return _list._list.cbegin(); }
        const_iterator cend() const { return _list._list.cend(); }

    private:
        SafeList& _list;
        ScopedLock _lock;
    };

    SafeList() = default;
    ~SafeList() override = default;

    // Move operations
    SafeList(SafeList&& other) noexcept : LockableBase(std::move(other)) {
        auto lock = other.lock();
        if (lock) {
            _list = std::move(other._list);
        }
    }

    SafeList& operator=(SafeList&& other) noexcept {
        if (this != &other) {
            auto lockThis = lock();
            auto lockOther = other.lock();
            if (lockThis && lockOther) {
                LockableBase::operator=(std::move(other));
                _list = std::move(other._list);
            }
        }
        return *this;
    }

    // ==================== Basic Operations ====================

    /**
     * @brief Checks if the list is empty.
     * @return true if empty.
     */
    [[nodiscard]] bool empty() const {
        auto guard = lock();
        return guard ? _list.empty() : true;
    }

    /**
     * @brief Returns the number of elements.
     * @return Size of the list, or 0 if lock fails.
     */
    [[nodiscard]] size_type size() const {
        auto guard = lock();
        return guard ? _list.size() : 0;
    }

    /**
     * @brief Clears all elements from the list.
     */
    void clear() {
        auto guard = lock();
        if (guard) {
            _list.clear();
        }
    }

    // ==================== Element Access ====================

    /**
     * @brief Returns the first element.
     * @return std::optional with value if exists and lock acquired.
     */
    [[nodiscard]] std::optional<T> front() const {
        auto guard = lock();
        if (guard && !_list.empty()) {
            return _list.front();
        }
        return std::nullopt;
    }

    /**
     * @brief Returns the last element.
     * @return std::optional with value if exists and lock acquired.
     */
    [[nodiscard]] std::optional<T> back() const {
        auto guard = lock();
        if (guard && !_list.empty()) {
            return _list.back();
        }
        return std::nullopt;
    }

    // ==================== Modifiers ====================

    /**
     * @brief Adds element to the front.
     * @param value Value to add.
     * @return true if successful.
     */
    bool pushFront(const T& value) {
        auto guard = lock();
        if (guard) {
            _list.push_front(value);
            return true;
        }
        return false;
    }

    /**
     * @brief Adds element to the front (move version).
     */
    bool pushFront(T&& value) {
        auto guard = lock();
        if (guard) {
            _list.push_front(std::move(value));
            return true;
        }
        return false;
    }

    /**
     * @brief Adds element to the back.
     * @param value Value to add.
     * @return true if successful.
     */
    bool pushBack(const T& value) {
        auto guard = lock();
        if (guard) {
            _list.push_back(value);
            return true;
        }
        return false;
    }

    /**
     * @brief Adds element to the back (move version).
     */
    bool pushBack(T&& value) {
        auto guard = lock();
        if (guard) {
            _list.push_back(std::move(value));
            return true;
        }
        return false;
    }

    /**
     * @brief Constructs element in-place at the back.
     * @param args Arguments for element constructor.
     * @return true if successful.
     */
    template<typename... Args>
    bool emplaceBack(Args&&... args) {
        auto guard = lock();
        if (guard) {
            _list.emplace_back(std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    /**
     * @brief Removes and returns the first element.
     * @return std::optional with the removed value.
     */
    [[nodiscard]] std::optional<T> popFront() {
        auto guard = lock();
        if (guard && !_list.empty()) {
            T value = std::move(_list.front());
            _list.pop_front();
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Removes and returns the last element.
     * @return std::optional with the removed value.
     */
    [[nodiscard]] std::optional<T> popBack() {
        auto guard = lock();
        if (guard && !_list.empty()) {
            T value = std::move(_list.back());
            _list.pop_back();
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Removes first element matching predicate.
     * @param predicate Function returning true for element to remove.
     * @return true if element was removed.
     */
    bool removeIf(std::function<bool(const T&)> predicate) {
        auto guard = lock();
        if (guard) {
            auto it = std::find_if(_list.begin(), _list.end(), predicate);
            if (it != _list.end()) {
                _list.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Removes all elements matching predicate.
     * @param predicate Function returning true for elements to remove.
     * @return Number of elements removed.
     */
    size_type removeAllIf(std::function<bool(const T&)> predicate) {
        auto guard = lock();
        if (guard) {
            size_type oldSize = _list.size();
            _list.remove_if(predicate);
            return oldSize - _list.size();
        }
        return 0;
    }

    /**
     * @brief Removes first occurrence of value.
     * @param value Value to remove.
     * @return true if element was removed.
     */
    bool remove(const T& value) {
        return removeIf([&value](const T& item) { return item == value; });
    }

    // ==================== Search Operations ====================

    /**
     * @brief Checks if element exists.
     * @param value Value to search for.
     * @return true if found.
     */
    [[nodiscard]] bool contains(const T& value) const {
        auto guard = lock();
        if (guard) {
            return std::find(_list.begin(), _list.end(), value) != _list.end();
        }
        return false;
    }

    /**
     * @brief Finds first element matching predicate.
     * @param predicate Function returning true for desired element.
     * @return std::optional with found value.
     */
    [[nodiscard]] std::optional<T> find(std::function<bool(const T&)> predicate) const {
        auto guard = lock();
        if (guard) {
            auto it = std::find_if(_list.begin(), _list.end(), predicate);
            if (it != _list.end()) {
                return *it;
            }
        }
        return std::nullopt;
    }

    // ==================== Iteration ====================

    /**
     * @brief Executes function for each element.
     * @param func Function to apply to each element.
     */
    void forEach(std::function<void(const T&)> func) const {
        auto guard = lock();
        if (guard) {
            for (const auto& item : _list) {
                func(item);
            }
        }
    }

    /**
     * @brief Executes function for each element (mutable version).
     * @param func Function to apply to each element.
     */
    void forEachMut(std::function<void(T&)> func) {
        auto guard = lock();
        if (guard) {
            for (auto& item : _list) {
                func(item);
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
     * @brief Executes function with locked access to internal list.
     * @param func Function receiving reference to internal list.
     * @return true if function was executed.
     */
    bool withLock(std::function<void(std::list<T>&)> func) {
        auto guard = lock();
        if (guard) {
            func(_list);
            return true;
        }
        return false;
    }

    // ==================== Sorting ====================

    /**
     * @brief Sorts the list using the provided comparison function.
     * @param compare Comparison function.
     */
    void sort(std::function<bool(const T&, const T&)> compare) {
        auto guard = lock();
        if (guard) {
            _list.sort(compare);
        }
    }

    /**
     * @brief Sorts the list using default comparison (operator<).
     */
    void sort() {
        auto guard = lock();
        if (guard) {
            _list.sort();
        }
    }

    // ==================== Bulk Operations ====================

    /**
     * @brief Copies all elements to a vector.
     * @return Vector containing all elements.
     */
    [[nodiscard]] std::vector<T> toVector() const {
        auto guard = lock();
        if (guard) {
            return std::vector<T>(_list.begin(), _list.end());
        }
        return {};
    }

    /**
     * @brief Adds all elements from a range.
     * @param begin Begin iterator.
     * @param end End iterator.
     * @return true if successful.
     */
    template<typename InputIt>
    bool insertRange(InputIt begin, InputIt end) {
        auto guard = lock();
        if (guard) {
            _list.insert(_list.end(), begin, end);
            return true;
        }
        return false;
    }

private:
    std::list<T> _list;
};

} // namespace SafeContainers

#endif // SAFE_LIST_H

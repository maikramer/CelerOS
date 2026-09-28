/**
 * @file SafeVector.h
 * @brief Thread-safe dynamic array using FreeRTOS mutex
 * @author maikeu
 * @date 2024
 * 
 * Provides a thread-safe wrapper around std::vector with RAII-based locking.
 */

#ifndef SAFE_VECTOR_H
#define SAFE_VECTOR_H

#include "LockableBase.h"
#include <vector>
#include <functional>
#include <optional>
#include <algorithm>

namespace SafeContainers {

/**
 * @class SafeVector
 * @brief Thread-safe vector container based on std::vector.
 * 
 * @tparam T The type of elements stored in the vector.
 * 
 * All operations are protected by a mutex. For iteration, use withLock() or
 * the iteration guard.
 * 
 * Example:
 * @code
 * SafeVector<int> vec;
 * vec.pushBack(42);
 * 
 * if (auto value = vec.at(0)) {
 *     printf("Value: %d\n", *value);
 * }
 * 
 * // Safe iteration
 * vec.forEach([](const int& val) {
 *     printf("Value: %d\n", val);
 * });
 * @endcode
 */
template<typename T>
class SafeVector : public LockableBase {
public:
    using value_type = T;
    using size_type = typename std::vector<T>::size_type;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    /**
     * @class IterationGuard
     * @brief RAII guard for safe iteration over the vector.
     */
    class IterationGuard {
    public:
        IterationGuard(SafeVector& vec, uint32_t timeoutMs)
            : _vec(vec), _lock(vec.getMutex(), pdMS_TO_TICKS(timeoutMs)) {}

        ~IterationGuard() = default;

        IterationGuard(const IterationGuard&) = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

        IterationGuard(IterationGuard&& other) noexcept
            : _vec(other._vec), _lock(std::move(other._lock)) {}

        [[nodiscard]] bool isLocked() const { return _lock.isLocked(); }
        explicit operator bool() const { return _lock.isLocked(); }

        [[nodiscard]] std::vector<T>& get() { return _vec._vector; }
        [[nodiscard]] const std::vector<T>& get() const { return _vec._vector; }

        iterator begin() { return _vec._vector.begin(); }
        iterator end() { return _vec._vector.end(); }
        const_iterator begin() const { return _vec._vector.begin(); }
        const_iterator end() const { return _vec._vector.end(); }
        const_iterator cbegin() const { return _vec._vector.cbegin(); }
        const_iterator cend() const { return _vec._vector.cend(); }

        T& operator[](size_type index) { return _vec._vector[index]; }
        const T& operator[](size_type index) const { return _vec._vector[index]; }

    private:
        SafeVector& _vec;
        ScopedLock _lock;
    };

    SafeVector() = default;
    
    /**
     * @brief Constructor with initial capacity.
     * @param capacity Initial capacity to reserve.
     */
    explicit SafeVector(size_type capacity) {
        _vector.reserve(capacity);
    }

    ~SafeVector() override = default;

    // Move operations
    SafeVector(SafeVector&& other) noexcept : LockableBase(std::move(other)) {
        auto lockOther = other.lock();
        if (lockOther) {
            _vector = std::move(other._vector);
        }
    }

    SafeVector& operator=(SafeVector&& other) noexcept {
        if (this != &other) {
            auto lockThis = lock();
            auto lockOther = other.lock();
            if (lockThis && lockOther) {
                LockableBase::operator=(std::move(other));
                _vector = std::move(other._vector);
            }
        }
        return *this;
    }

    // ==================== Basic Operations ====================

    /**
     * @brief Checks if the vector is empty.
     * @return true if empty.
     */
    [[nodiscard]] bool empty() const {
        auto guard = lock();
        return guard ? _vector.empty() : true;
    }

    /**
     * @brief Returns the number of elements.
     * @return Size of the vector.
     */
    [[nodiscard]] size_type size() const {
        auto guard = lock();
        return guard ? _vector.size() : 0;
    }

    /**
     * @brief Returns the capacity.
     * @return Current capacity.
     */
    [[nodiscard]] size_type capacity() const {
        auto guard = lock();
        return guard ? _vector.capacity() : 0;
    }

    /**
     * @brief Clears all elements from the vector.
     */
    void clear() {
        auto guard = lock();
        if (guard) {
            _vector.clear();
        }
    }

    /**
     * @brief Reserves capacity for at least n elements.
     * @param n Minimum capacity.
     */
    void reserve(size_type n) {
        auto guard = lock();
        if (guard) {
            _vector.reserve(n);
        }
    }

    /**
     * @brief Shrinks capacity to fit size.
     */
    void shrinkToFit() {
        auto guard = lock();
        if (guard) {
            _vector.shrink_to_fit();
        }
    }

    // ==================== Element Access ====================

    /**
     * @brief Access element at index with bounds checking.
     * @param index Index of element.
     * @return std::optional with value if valid index.
     */
    [[nodiscard]] std::optional<T> at(size_type index) const {
        auto guard = lock();
        if (guard && index < _vector.size()) {
            return _vector[index];
        }
        return std::nullopt;
    }

    /**
     * @brief Access element with default value for out-of-bounds.
     * @param index Index of element.
     * @param defaultValue Default if out of bounds.
     * @return Element or default.
     */
    [[nodiscard]] T atOrDefault(size_type index, const T& defaultValue) const {
        auto guard = lock();
        if (guard && index < _vector.size()) {
            return _vector[index];
        }
        return defaultValue;
    }

    /**
     * @brief Returns the first element.
     * @return std::optional with value if exists.
     */
    [[nodiscard]] std::optional<T> front() const {
        auto guard = lock();
        if (guard && !_vector.empty()) {
            return _vector.front();
        }
        return std::nullopt;
    }

    /**
     * @brief Returns the last element.
     * @return std::optional with value if exists.
     */
    [[nodiscard]] std::optional<T> back() const {
        auto guard = lock();
        if (guard && !_vector.empty()) {
            return _vector.back();
        }
        return std::nullopt;
    }

    // ==================== Modifiers ====================

    /**
     * @brief Adds element to the end.
     * @param value Value to add.
     * @return true if successful.
     */
    bool pushBack(const T& value) {
        auto guard = lock();
        if (guard) {
            _vector.push_back(value);
            return true;
        }
        return false;
    }

    /**
     * @brief Adds element to the end (move version).
     */
    bool pushBack(T&& value) {
        auto guard = lock();
        if (guard) {
            _vector.push_back(std::move(value));
            return true;
        }
        return false;
    }

    /**
     * @brief Constructs element in-place at the end.
     * @param args Arguments for element constructor.
     * @return true if successful.
     */
    template<typename... Args>
    bool emplaceBack(Args&&... args) {
        auto guard = lock();
        if (guard) {
            _vector.emplace_back(std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    /**
     * @brief Removes and returns the last element.
     * @return std::optional with the removed value.
     */
    [[nodiscard]] std::optional<T> popBack() {
        auto guard = lock();
        if (guard && !_vector.empty()) {
            T value = std::move(_vector.back());
            _vector.pop_back();
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Sets value at index.
     * @param index Index to modify.
     * @param value New value.
     * @return true if successful (valid index).
     */
    bool set(size_type index, const T& value) {
        auto guard = lock();
        if (guard && index < _vector.size()) {
            _vector[index] = value;
            return true;
        }
        return false;
    }

    /**
     * @brief Inserts element at position.
     * @param index Position to insert.
     * @param value Value to insert.
     * @return true if successful.
     */
    bool insert(size_type index, const T& value) {
        auto guard = lock();
        if (guard && index <= _vector.size()) {
            _vector.insert(_vector.begin() + index, value);
            return true;
        }
        return false;
    }

    /**
     * @brief Removes element at index.
     * @param index Index to remove.
     * @return std::optional with removed value.
     */
    [[nodiscard]] std::optional<T> erase(size_type index) {
        auto guard = lock();
        if (guard && index < _vector.size()) {
            T value = std::move(_vector[index]);
            _vector.erase(_vector.begin() + index);
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
            auto it = std::find_if(_vector.begin(), _vector.end(), predicate);
            if (it != _vector.end()) {
                _vector.erase(it);
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
            size_type oldSize = _vector.size();
            _vector.erase(std::remove_if(_vector.begin(), _vector.end(), predicate),
                          _vector.end());
            return oldSize - _vector.size();
        }
        return 0;
    }

    /**
     * @brief Resizes the vector.
     * @param newSize New size.
     * @param value Value for new elements.
     */
    void resize(size_type newSize, const T& value = T{}) {
        auto guard = lock();
        if (guard) {
            _vector.resize(newSize, value);
        }
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
            return std::find(_vector.begin(), _vector.end(), value) != _vector.end();
        }
        return false;
    }

    /**
     * @brief Finds index of first occurrence.
     * @param value Value to search for.
     * @return std::optional with index if found.
     */
    [[nodiscard]] std::optional<size_type> indexOf(const T& value) const {
        auto guard = lock();
        if (guard) {
            auto it = std::find(_vector.begin(), _vector.end(), value);
            if (it != _vector.end()) {
                return std::distance(_vector.begin(), it);
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Finds first element matching predicate.
     * @param predicate Function returning true for desired element.
     * @return std::optional with found value.
     */
    [[nodiscard]] std::optional<T> find(std::function<bool(const T&)> predicate) const {
        auto guard = lock();
        if (guard) {
            auto it = std::find_if(_vector.begin(), _vector.end(), predicate);
            if (it != _vector.end()) {
                return *it;
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Counts elements matching predicate.
     * @param predicate Function returning true for elements to count.
     * @return Number of matching elements.
     */
    [[nodiscard]] size_type count(std::function<bool(const T&)> predicate) const {
        auto guard = lock();
        if (guard) {
            return std::count_if(_vector.begin(), _vector.end(), predicate);
        }
        return 0;
    }

    // ==================== Iteration ====================

    /**
     * @brief Executes function for each element.
     * @param func Function to apply.
     */
    void forEach(std::function<void(const T&)> func) const {
        auto guard = lock();
        if (guard) {
            for (const auto& item : _vector) {
                func(item);
            }
        }
    }

    /**
     * @brief Executes function for each element (mutable version).
     * @param func Function to apply.
     */
    void forEachMut(std::function<void(T&)> func) {
        auto guard = lock();
        if (guard) {
            for (auto& item : _vector) {
                func(item);
            }
        }
    }

    /**
     * @brief Executes function for each element with index.
     * @param func Function receiving index and element.
     */
    void forEachIndexed(std::function<void(size_type, const T&)> func) const {
        auto guard = lock();
        if (guard) {
            for (size_type i = 0; i < _vector.size(); ++i) {
                func(i, _vector[i]);
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
     * @brief Executes function with locked access to internal vector.
     * @param func Function receiving reference to internal vector.
     * @return true if function was executed.
     */
    bool withLock(std::function<void(std::vector<T>&)> func) {
        auto guard = lock();
        if (guard) {
            func(_vector);
            return true;
        }
        return false;
    }

    // ==================== Sorting ====================

    /**
     * @brief Sorts using comparison function.
     * @param compare Comparison function.
     */
    void sort(std::function<bool(const T&, const T&)> compare) {
        auto guard = lock();
        if (guard) {
            std::sort(_vector.begin(), _vector.end(), compare);
        }
    }

    /**
     * @brief Sorts using default comparison (operator<).
     */
    void sort() {
        auto guard = lock();
        if (guard) {
            std::sort(_vector.begin(), _vector.end());
        }
    }

    // ==================== Bulk Operations ====================

    /**
     * @brief Creates a copy of the internal vector.
     * @return Copy of all elements.
     */
    [[nodiscard]] std::vector<T> copy() const {
        auto guard = lock();
        if (guard) {
            return _vector;
        }
        return {};
    }

    /**
     * @brief Appends elements from another container.
     * @param begin Begin iterator.
     * @param end End iterator.
     * @return true if successful.
     */
    template<typename InputIt>
    bool append(InputIt begin, InputIt end) {
        auto guard = lock();
        if (guard) {
            _vector.insert(_vector.end(), begin, end);
            return true;
        }
        return false;
    }

    /**
     * @brief Transforms elements using a function.
     * @param func Transformation function.
     * @return Vector of transformed elements.
     */
    template<typename U>
    [[nodiscard]] std::vector<U> transform(std::function<U(const T&)> func) const {
        auto guard = lock();
        std::vector<U> result;
        if (guard) {
            result.reserve(_vector.size());
            for (const auto& item : _vector) {
                result.push_back(func(item));
            }
        }
        return result;
    }

    /**
     * @brief Filters elements by predicate.
     * @param predicate Filter function.
     * @return Vector of matching elements.
     */
    [[nodiscard]] std::vector<T> filter(std::function<bool(const T&)> predicate) const {
        auto guard = lock();
        std::vector<T> result;
        if (guard) {
            for (const auto& item : _vector) {
                if (predicate(item)) {
                    result.push_back(item);
                }
            }
        }
        return result;
    }

private:
    std::vector<T> _vector;
};

} // namespace SafeContainers

#endif // SAFE_VECTOR_H

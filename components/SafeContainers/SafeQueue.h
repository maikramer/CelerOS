/**
 * @file SafeQueue.h
 * @brief Thread-safe queue using FreeRTOS primitives
 * @author maikeu
 * @date 2024
 * 
 * Provides a thread-safe FIFO queue with blocking and non-blocking operations.
 * Can use either mutex-protected std::queue or native FreeRTOS queue.
 */

#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include "LockableBase.h"
#include <queue>
#include <optional>
#include <functional>

namespace SafeContainers {

/**
 * @class SafeQueue
 * @brief Thread-safe FIFO queue based on std::queue with mutex protection.
 * 
 * @tparam T The type of elements stored in the queue.
 * 
 * Ideal for producer-consumer patterns where thread-safety is required.
 * 
 * Example:
 * @code
 * SafeQueue<Message> messageQueue;
 * 
 * // Producer
 * messageQueue.push(Message{"Hello"});
 * 
 * // Consumer
 * while (auto msg = messageQueue.pop()) {
 *     processMessage(*msg);
 * }
 * @endcode
 */
template<typename T>
class SafeQueue : public LockableBase {
public:
    using value_type = T;
    using size_type = typename std::queue<T>::size_type;

    SafeQueue() = default;
    
    /**
     * @brief Constructor with maximum size limit.
     * @param maxSize Maximum number of elements (0 = unlimited).
     */
    explicit SafeQueue(size_type maxSize) : _maxSize(maxSize) {}

    ~SafeQueue() override = default;

    // Move operations
    SafeQueue(SafeQueue&& other) noexcept : LockableBase(std::move(other)) {
        auto lockOther = other.lock();
        if (lockOther) {
            _queue = std::move(other._queue);
            _maxSize = other._maxSize;
        }
    }

    SafeQueue& operator=(SafeQueue&& other) noexcept {
        if (this != &other) {
            auto lockThis = lock();
            auto lockOther = other.lock();
            if (lockThis && lockOther) {
                LockableBase::operator=(std::move(other));
                _queue = std::move(other._queue);
                _maxSize = other._maxSize;
            }
        }
        return *this;
    }

    // ==================== Basic Operations ====================

    /**
     * @brief Checks if the queue is empty.
     * @return true if empty.
     */
    [[nodiscard]] bool empty() const {
        auto guard = lock();
        return guard ? _queue.empty() : true;
    }

    /**
     * @brief Returns the number of elements.
     * @return Size of the queue.
     */
    [[nodiscard]] size_type size() const {
        auto guard = lock();
        return guard ? _queue.size() : 0;
    }

    /**
     * @brief Checks if queue is full (if max size is set).
     * @return true if full.
     */
    [[nodiscard]] bool full() const {
        if (_maxSize == 0) return false;
        auto guard = lock();
        return guard ? _queue.size() >= _maxSize : false;
    }

    /**
     * @brief Clears all elements from the queue.
     */
    void clear() {
        auto guard = lock();
        if (guard) {
            std::queue<T> empty;
            std::swap(_queue, empty);
        }
    }

    // ==================== Element Access ====================

    /**
     * @brief Returns the front element without removing it.
     * @return std::optional with value if exists.
     */
    [[nodiscard]] std::optional<T> front() const {
        auto guard = lock();
        if (guard && !_queue.empty()) {
            return _queue.front();
        }
        return std::nullopt;
    }

    /**
     * @brief Returns the back element without removing it.
     * @return std::optional with value if exists.
     */
    [[nodiscard]] std::optional<T> back() const {
        auto guard = lock();
        if (guard && !_queue.empty()) {
            return _queue.back();
        }
        return std::nullopt;
    }

    // ==================== Modifiers ====================

    /**
     * @brief Adds element to the back of the queue.
     * @param value Value to add.
     * @return true if successful (not full).
     */
    bool push(const T& value) {
        auto guard = lock();
        if (guard) {
            if (_maxSize > 0 && _queue.size() >= _maxSize) {
                return false;
            }
            _queue.push(value);
            return true;
        }
        return false;
    }

    /**
     * @brief Adds element to the back (move version).
     */
    bool push(T&& value) {
        auto guard = lock();
        if (guard) {
            if (_maxSize > 0 && _queue.size() >= _maxSize) {
                return false;
            }
            _queue.push(std::move(value));
            return true;
        }
        return false;
    }

    /**
     * @brief Constructs element in-place.
     * @param args Arguments for element constructor.
     * @return true if successful.
     */
    template<typename... Args>
    bool emplace(Args&&... args) {
        auto guard = lock();
        if (guard) {
            if (_maxSize > 0 && _queue.size() >= _maxSize) {
                return false;
            }
            _queue.emplace(std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    /**
     * @brief Removes and returns the front element.
     * @return std::optional with the removed value.
     */
    [[nodiscard]] std::optional<T> pop() {
        auto guard = lock();
        if (guard && !_queue.empty()) {
            T value = std::move(_queue.front());
            _queue.pop();
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Tries to add, discarding oldest if full.
     * @param value Value to add.
     * @return Discarded value if queue was full.
     */
    std::optional<T> pushOverwrite(const T& value) {
        auto guard = lock();
        std::optional<T> discarded;
        if (guard) {
            if (_maxSize > 0 && _queue.size() >= _maxSize) {
                discarded = std::move(_queue.front());
                _queue.pop();
            }
            _queue.push(value);
        }
        return discarded;
    }

    // ==================== Bulk Operations ====================

    /**
     * @brief Pops all elements into a vector.
     * @return Vector of all elements in queue order.
     */
    [[nodiscard]] std::vector<T> popAll() {
        auto guard = lock();
        std::vector<T> result;
        if (guard) {
            result.reserve(_queue.size());
            while (!_queue.empty()) {
                result.push_back(std::move(_queue.front()));
                _queue.pop();
            }
        }
        return result;
    }

    /**
     * @brief Pops up to N elements.
     * @param maxCount Maximum elements to pop.
     * @return Vector of popped elements.
     */
    [[nodiscard]] std::vector<T> popN(size_type maxCount) {
        auto guard = lock();
        std::vector<T> result;
        if (guard) {
            size_type count = std::min(maxCount, _queue.size());
            result.reserve(count);
            for (size_type i = 0; i < count; ++i) {
                result.push_back(std::move(_queue.front()));
                _queue.pop();
            }
        }
        return result;
    }

    /**
     * @brief Processes all elements without removing them.
     * @param func Function to apply to each element.
     */
    void forEach(std::function<void(const T&)> func) const {
        auto guard = lock();
        if (guard) {
            // Need to copy for iteration since std::queue doesn't have iterators
            std::queue<T> temp = _queue;
            while (!temp.empty()) {
                func(temp.front());
                temp.pop();
            }
        }
    }

    /**
     * @brief Processes and removes elements while predicate returns true.
     * @param func Predicate - return true to continue, false to stop.
     * @return Number of elements processed.
     */
    size_type processWhile(std::function<bool(T&)> func) {
        auto guard = lock();
        size_type count = 0;
        if (guard) {
            while (!_queue.empty()) {
                if (!func(_queue.front())) {
                    break;
                }
                _queue.pop();
                ++count;
            }
        }
        return count;
    }

    // ==================== Configuration ====================

    /**
     * @brief Sets maximum queue size.
     * @param maxSize Maximum size (0 = unlimited).
     */
    void setMaxSize(size_type maxSize) {
        auto guard = lock();
        if (guard) {
            _maxSize = maxSize;
        }
    }

    /**
     * @brief Gets maximum queue size.
     * @return Maximum size (0 = unlimited).
     */
    [[nodiscard]] size_type maxSize() const {
        return _maxSize;
    }

private:
    std::queue<T> _queue;
    size_type _maxSize = 0;
};

/**
 * @class NativeQueue
 * @brief Thread-safe queue using FreeRTOS native queue.
 * 
 * Uses FreeRTOS queue directly for potentially better performance
 * with fixed-size elements. Supports blocking operations.
 * 
 * @tparam T Type of elements (must be trivially copyable).
 */
template<typename T>
class NativeQueue {
    static_assert(std::is_trivially_copyable_v<T>, 
                  "NativeQueue requires trivially copyable types");

public:
    /**
     * @brief Constructor.
     * @param length Maximum number of items in queue.
     */
    explicit NativeQueue(size_t length) 
        : _queue(xQueueCreate(length, sizeof(T))), _length(length) {
        if (_queue == nullptr) {
            ESP_LOGE("NativeQueue", "Failed to create queue!");
        }
    }

    ~NativeQueue() {
        if (_queue != nullptr) {
            vQueueDelete(_queue);
        }
    }

    // Non-copyable, non-movable
    NativeQueue(const NativeQueue&) = delete;
    NativeQueue& operator=(const NativeQueue&) = delete;
    NativeQueue(NativeQueue&&) = delete;
    NativeQueue& operator=(NativeQueue&&) = delete;

    /**
     * @brief Checks if queue is valid.
     */
    [[nodiscard]] bool isValid() const { return _queue != nullptr; }

    /**
     * @brief Checks if queue is empty.
     */
    [[nodiscard]] bool empty() const {
        return _queue ? uxQueueMessagesWaiting(_queue) == 0 : true;
    }

    /**
     * @brief Gets number of items in queue.
     */
    [[nodiscard]] size_t size() const {
        return _queue ? uxQueueMessagesWaiting(_queue) : 0;
    }

    /**
     * @brief Gets available space.
     */
    [[nodiscard]] size_t available() const {
        return _queue ? uxQueueSpacesAvailable(_queue) : 0;
    }

    /**
     * @brief Checks if queue is full.
     */
    [[nodiscard]] bool full() const {
        return _queue ? uxQueueSpacesAvailable(_queue) == 0 : true;
    }

    /**
     * @brief Sends item to back of queue (blocking).
     * @param item Item to send.
     * @param timeoutMs Timeout in milliseconds (portMAX_DELAY for infinite).
     * @return true if successful.
     */
    bool send(const T& item, uint32_t timeoutMs = portMAX_DELAY) {
        if (_queue == nullptr) return false;
        TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        return xQueueSend(_queue, &item, ticks) == pdTRUE;
    }

    /**
     * @brief Sends item to front of queue (blocking).
     * @param item Item to send.
     * @param timeoutMs Timeout in milliseconds.
     * @return true if successful.
     */
    bool sendToFront(const T& item, uint32_t timeoutMs = portMAX_DELAY) {
        if (_queue == nullptr) return false;
        TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        return xQueueSendToFront(_queue, &item, ticks) == pdTRUE;
    }

    /**
     * @brief Receives item from queue (blocking).
     * @param item Reference to store received item.
     * @param timeoutMs Timeout in milliseconds (portMAX_DELAY for infinite).
     * @return true if item received.
     */
    bool receive(T& item, uint32_t timeoutMs = portMAX_DELAY) {
        if (_queue == nullptr) return false;
        TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        return xQueueReceive(_queue, &item, ticks) == pdTRUE;
    }

    /**
     * @brief Receives item from queue (optional version).
     * @param timeoutMs Timeout in milliseconds.
     * @return std::optional with item if received.
     */
    [[nodiscard]] std::optional<T> receive(uint32_t timeoutMs = 0) {
        T item;
        if (receive(item, timeoutMs)) {
            return item;
        }
        return std::nullopt;
    }

    /**
     * @brief Peeks at front item without removing.
     * @param item Reference to store peeked item.
     * @return true if item exists.
     */
    bool peek(T& item) const {
        if (_queue == nullptr) return false;
        return xQueuePeek(_queue, &item, 0) == pdTRUE;
    }

    /**
     * @brief Clears all items from queue.
     */
    void clear() {
        if (_queue) {
            xQueueReset(_queue);
        }
    }

    /**
     * @brief Sends from ISR context.
     * @param item Item to send.
     * @param higherPriorityTaskWoken Set to true if a task was woken.
     * @return true if successful.
     */
    bool sendFromISR(const T& item, BaseType_t* higherPriorityTaskWoken = nullptr) {
        if (_queue == nullptr) return false;
        BaseType_t dummy = pdFALSE;
        return xQueueSendFromISR(_queue, &item, 
                                 higherPriorityTaskWoken ? higherPriorityTaskWoken : &dummy) == pdTRUE;
    }

    /**
     * @brief Receives from ISR context.
     * @param item Reference to store item.
     * @param higherPriorityTaskWoken Set to true if a task was woken.
     * @return true if item received.
     */
    bool receiveFromISR(T& item, BaseType_t* higherPriorityTaskWoken = nullptr) {
        if (_queue == nullptr) return false;
        BaseType_t dummy = pdFALSE;
        return xQueueReceiveFromISR(_queue, &item,
                                    higherPriorityTaskWoken ? higherPriorityTaskWoken : &dummy) == pdTRUE;
    }

private:
    QueueHandle_t _queue;
    size_t _length;
};

} // namespace SafeContainers

#endif // SAFE_QUEUE_H

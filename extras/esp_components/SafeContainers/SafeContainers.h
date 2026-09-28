/**
 * @file SafeContainers.h
 * @brief Main header - includes all thread-safe container types
 * @author maikeu
 * @date 2024
 * 
 * Include this single header to access all SafeContainers types.
 */

#ifndef SAFE_CONTAINERS_H
#define SAFE_CONTAINERS_H

#include "ScopedLock.h"
#include "LockableBase.h"
#include "SafeList.h"
#include "SafeMap.h"
#include "SafeQueue.h"
#include "SafeVector.h"

/**
 * @namespace SafeContainers
 * @brief Thread-safe container library for ESP32/FreeRTOS
 * 
 * This namespace provides thread-safe wrappers for common STL containers,
 * using FreeRTOS mutex primitives for synchronization.
 * 
 * ## Features
 * 
 * - **RAII-based locking**: ScopedLock and IterationGuard prevent forgotten unlocks
 * - **Modern C++ API**: Uses std::optional, std::function, move semantics
 * - **Backward compatible**: Legacy methods marked deprecated but still work
 * - **Type-safe**: Template-based for compile-time type checking
 * 
 * ## Available Containers
 * 
 * - `SafeList<T>`: Thread-safe doubly-linked list
 * - `SafeMap<K, V>`: Thread-safe associative container
 * - `SafeQueue<T>`: Thread-safe FIFO queue
 * - `SafeVector<T>`: Thread-safe dynamic array
 * - `NativeQueue<T>`: FreeRTOS native queue wrapper
 * 
 * ## Utilities
 * 
 * - `ScopedLock`: RAII lock wrapper
 * - `RecursiveScopedLock`: RAII recursive lock wrapper
 * - `LockableBase`: Base class for lockable containers
 * 
 * ## Quick Start
 * 
 * @code
 * #include "SafeContainers.h"
 * using namespace SafeContainers;
 * 
 * // Create containers
 * SafeList<int> numbers;
 * SafeMap<std::string, int> scores;
 * SafeQueue<Message> messageQueue;
 * SafeVector<Sensor> sensors;
 * 
 * // Thread-safe operations
 * numbers.pushBack(42);
 * scores.insert("player1", 100);
 * messageQueue.push(Message{"hello"});
 * sensors.emplaceBack("temp", 25.0f);
 * 
 * // Safe iteration
 * numbers.forEach([](int n) {
 *     printf("%d\n", n);
 * });
 * 
 * // Manual iteration with guard
 * {
 *     auto guard = scores.iterationGuard();
 *     if (guard) {
 *         for (const auto& [name, score] : guard) {
 *             printf("%s: %d\n", name.c_str(), score);
 *         }
 *     }
 * }
 * @endcode
 * 
 * ## Thread Safety Guarantees
 * 
 * - All public methods are thread-safe
 * - Operations are atomic within method boundaries
 * - Iterators are only valid within IterationGuard scope
 * - Lock timeout defaults to 1000ms
 * 
 * ## Performance Considerations
 * 
 * - Each operation acquires/releases mutex
 * - Prefer bulk operations (forEach, withLock) over multiple single operations
 * - Use NativeQueue for high-frequency producer-consumer patterns
 * - Consider lock contention in high-throughput scenarios
 */
namespace SafeContainers {
    // All types are already defined in their respective headers
}

#endif // SAFE_CONTAINERS_H

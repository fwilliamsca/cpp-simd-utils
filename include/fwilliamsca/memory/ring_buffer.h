/**
 * @file ring_buffer.h
 * @brief Single-Producer Single-Consumer (SPSC) Lock-Free Queue.
 * @author F.Williams
 * * Optimized for Tick-To-Trade latency.
 * - Enforces explicit cache-line alignment to prevent False Sharing.
 * - Uses std::memory_order_release/acquire for minimal synchronization overhead.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

namespace fwilliamsca {
namespace memory {

    template <typename T, size_t Capacity>
    class SPSCRingBuffer {
        static_assert((Capacity != 0) && ((Capacity & (Capacity - 1)) == 0), 
                      "Capacity must be a power of 2 for bitwise wrapping optimization.");

    public:
        using value_type = T;

        SPSCRingBuffer() {
            // Allocate aligned memory for the buffer
            void* ptr = nullptr;
            if (posix_memalign(&ptr, CACHE_LINE_SIZE, sizeof(T) * Capacity) != 0) {
                throw std::bad_alloc();
            }
            buffer_ = static_cast<T*>(ptr);
        }

        ~SPSCRingBuffer() {
            free(buffer_);
        }

        /**
         * @brief Enqueues an item using move semantics (Zero-Copy).
         * @return true if successful, false if buffer is full.
         * * Memory Ordering:
         * We use memory_order_release to ensure the write to the buffer 
         * is visible before the update to tail_.
         */
        template <typename U>
        bool try_push(U&& item) {
            const size_t current_tail = tail_.load(std::memory_order_relaxed);
            const size_t next_tail = (current_tail + 1) & (Capacity - 1);

            // Check if full (acquire load on head to see consumer's updates)
            if (next_tail == head_.load(std::memory_order_acquire)) {
                return false; 
            }

            // Construct in-place
            new (&buffer_[current_tail]) T(std::forward<U>(item));

            // Commit the push
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }

        /**
         * @brief Dequeues an item.
         * @return true if successful, false if buffer is empty.
         */
        bool try_pop(T& out_item) {
            const size_t current_head = head_.load(std::memory_order_relaxed);

            // Check if empty (acquire load on tail to see producer's updates)
            if (current_head == tail_.load(std::memory_order_acquire)) {
                return false;
            }

            // Move assignment
            out_item = std::move(buffer_[current_head]);
            
            // Destruct the object in the buffer
            buffer_[current_head].~T();

            const size_t next_head = (current_head + 1) & (Capacity - 1);
            head_.store(next_head, std::memory_order_release);
            
            return true;
        }

        /**
         * @brief Prefetches the next cache line into L1 cache.
         * Useful for latency-critical loops.
         */
        void prefetch_next() const {
            size_t next = (head_.load(std::memory_order_relaxed) + 1) & (Capacity - 1);
            __builtin_prefetch(&buffer_[next], 0, 3);
        }

    private:
        // PADDING 1: Prevent false sharing with adjacent objects
        char pad0_[CACHE_LINE_SIZE];

        // Head index (Consumer owns this)
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};

        // PADDING 2: Prevent false sharing between head and tail
        char pad1_[CACHE_LINE_SIZE];

        // Tail index (Producer owns this)
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};

        // PADDING 3: Prevent false sharing between tail and buffer ptr
        char pad2_[CACHE_LINE_SIZE];

        T* buffer_;
    };

} // namespace memory
} // namespace fwilliamsca
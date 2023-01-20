#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

// Architecture-specific cache line size (L1 Cache)
constexpr size_t CACHE_LINE_SIZE = 64;

namespace Base58::Core {

    /**
     * @brief Single-Producer Single-Consumer (SPSC) Lock-Free Ring Buffer.
     * Optimized for nanosecond-level latency in trading engines.
     * * WARNING: This implementation assumes x86_64 TSO memory model.
     * Use with caution on ARM weak-memory architectures.
     */
    template<typename T, size_t Capacity>
    class alignas(CACHE_LINE_SIZE) RingBuffer {
    public:
        explicit RingBuffer() noexcept : head_(0), tail_(0) {
            // Static assertion to ensure lock-free operations are supported by hardware
            static_assert(std::atomic<size_t>::is_always_lock_free, "Atomic size_t is not lock-free");
            buffer_.resize(Capacity);
        }

        /**
         * @brief Zero-copy enqueue operation.
         * Uses acquire-release semantics to ensure visibility without full memory barriers.
         */
        template<typename U>
        bool enqueue(U&& item) noexcept {
            const size_t current_tail = tail_.load(std::memory_order_relaxed);
            const size_t next_tail = (current_tail + 1) % Capacity;

            if (next_tail == head_.load(std::memory_order_acquire)) {
                return false; // Buffer Full
            }

            buffer_[current_tail] = std::forward<U>(item);
            tail_.store(next_tail, std::memory_order_release);
            return true;
        }

        bool dequeue(T& item) noexcept {
            const size_t current_head = head_.load(std::memory_order_relaxed);

            if (current_head == tail_.load(std::memory_order_acquire)) {
                return false; // Buffer Empty
            }

            item = std::move(buffer_[current_head]);
            head_.store((current_head + 1) % Capacity, std::memory_order_release);
            return true;
        }

    private:
        // Padding to prevent False Sharing between head and tail indicators
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
        char pad1_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];

        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
        char pad2_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];

        std::vector<T> buffer_;
    };
}
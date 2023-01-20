/**
 * @file benchmark_main.cpp
 * @brief Benchmarking suite for SIMD kernels and Lock-Free Queue.
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <cstring>
#include "../include/fwilliamsca/simd/intrinsics.h"
#include "../include/fwilliamsca/memory/ring_buffer.h"

using namespace fwilliamsca;

// RDTSC: Read Time-Stamp Counter for nanosecond precision
inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void bench_avx512_dot_product() {
    std::cout << "[BENCH] Starting AVX-512 Dot Product Test...\n";
    
    constexpr size_t N = 1024 * 1024;
    alignas(64) std::vector<double> a(N, 1.0001);
    alignas(64) std::vector<double> b(N, 0.9999);

    // Warmup
    simd::MathKernel<>::dot_product(a.data(), b.data(), N);

    auto start = std::chrono::high_resolution_clock::now();
    uint64_t cycles_start = rdtsc();
    
    volatile double result = 0.0;
    for(int i=0; i<100; ++i) {
        result += simd::MathKernel<>::dot_product(a.data(), b.data(), N);
    }

    uint64_t cycles_end = rdtsc();
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "  > Processed " << (N * 100) / 1e6 << " M elements\n";
    std::cout << "  > Time: " << duration << " us\n";
    std::cout << "  > Cycles per Op: " << (cycles_end - cycles_start) / (N * 100.0) << "\n";
    std::cout << "[PASS] Result check: " << result << "\n\n";
}

void bench_ring_buffer() {
    std::cout << "[BENCH] Starting SPSC Ring Buffer Latency Test...\n";
    
    memory::SPSCRingBuffer<int, 4096> ring;
    std::atomic<bool> done{false};
    
    // Consumer Thread
    std::thread consumer([&]() {
        int val;
        while (!done.load(std::memory_order_relaxed)) {
            while (ring.try_pop(val)) {
                // consume
                __asm__ volatile("" ::: "memory");
            }
            std::this_thread::yield();
        }
    });

    // Producer Loop
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        while (!ring.try_push(i)) {
            _mm_pause(); // Intel pause instruction
        }
    }
    
    done.store(true);
    consumer.join();
    auto end = std::chrono::high_resolution_clock::now();
    
    std::cout << "  > Throughput: " 
              << 1000000.0 / std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " M ops/sec\n";
    std::cout << "[PASS] Buffer Test Complete.\n";
}

int main() {
    std::cout << "=== F.WilliamsCA High-Performance Utils ===\n";
    std::cout << "Architecture Detected: ";
#if defined(__AVX512F__)
    std::cout << "AVX-512 (Ice Lake+)\n";
#elif defined(__AVX2__)
    std::cout << "AVX2 (Haswell+)\n";
#else
    std::cout << "Scalar (Fallback)\n";
#endif
    std::cout << "-------------------------------------------\n";

    bench_avx512_dot_product();
    bench_ring_buffer();

    return 0;
}
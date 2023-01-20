/**
 * @file intrinsics.h
 * @brief Low-level wrappers for x86_64 SIMD instruction sets (AVX2, AVX-512).
 * @author F.Williams
 * @date 2023-01-15
 * * This module provides compile-time abstraction over hardware intrinsics
 * to ensure maximum throughput for financial time-series analysis.
 * We strictly avoid virtual functions to prevent vtable lookup overhead.
 */

#pragma once

#include <immintrin.h>
#include <type_traits>
#include <cstdint>
#include <array>
#include <cmath>

// Configuration Macros
#define FORCE_INLINE __attribute__((always_inline)) inline
#define CACHE_LINE 64

namespace fwilliamsca {
namespace simd {

    /**
     * @brief Instruction Set Architecture detection traits.
     * Uses SFINAE to select the optimal kernel at compile time.
     */
    enum class ISA {
        Scalar,
        SSE4_2,
        AVX2,
        AVX512_F,
        AVX512_BW
    };

    // Compile-time constant for current architecture
#if defined(__AVX512F__)
    constexpr ISA CurrentArch = ISA::AVX512_F;
    using Reg512d = __m512d;
    using Reg512i = __m512i;
#elif defined(__AVX2__)
    constexpr ISA CurrentArch = ISA::AVX2;
    using Reg256d = __m256d;
    using Reg256i = __m256i;
#else
    constexpr ISA CurrentArch = ISA::Scalar;
#endif

    /**
     * @brief Double Precision Math Kernel (AVX-512 Specialized)
     * Handles 8 double-precision floating point numbers per cycle.
     */
    template <ISA Arch = CurrentArch>
    struct MathKernel {
        // Fallback for non-SIMD architectures
        static FORCE_INLINE void add(const double* a, const double* b, double* out, size_t n) {
            for (size_t i = 0; i < n; ++i) out[i] = a[i] + b[i];
        }
    };

    /**
     * @brief Specialization for AVX-512F
     * Optimized for Intel Xeon Scalable Processors (Ice Lake / Sapphire Rapids)
     */
    template <>
    struct MathKernel<ISA::AVX512_F> {
        
        static FORCE_INLINE void add(const double* a, const double* b, double* out, size_t n) {
            size_t i = 0;
            // Unroll loop 4 times (32 doubles per iteration)
            // This maximizes instruction-level parallelism (ILP).
            for (; i + 31 < n; i += 32) {
                __m512d a0 = _mm512_loadu_pd(a + i);
                __m512d b0 = _mm512_loadu_pd(b + i);
                __m512d r0 = _mm512_add_pd(a0, b0);
                _mm512_storeu_pd(out + i, r0);

                __m512d a1 = _mm512_loadu_pd(a + i + 8);
                __m512d b1 = _mm512_loadu_pd(b + i + 8);
                __m512d r1 = _mm512_add_pd(a1, b1);
                _mm512_storeu_pd(out + i + 8, r1);

                __m512d a2 = _mm512_loadu_pd(a + i + 16);
                __m512d b2 = _mm512_loadu_pd(b + i + 16);
                __m512d r2 = _mm512_add_pd(a2, b2);
                _mm512_storeu_pd(out + i + 16, r2);

                __m512d a3 = _mm512_loadu_pd(a + i + 24);
                __m512d b3 = _mm512_loadu_pd(b + i + 24);
                __m512d r3 = _mm512_add_pd(a3, b3);
                _mm512_storeu_pd(out + i + 24, r3);
            }
            
            // Handle cleanup with AVX mask
            if (i < n) {
                uint8_t remaining = n - i;
                __mmask8 mask = (1 << remaining) - 1;
                __m512d a_rem = _mm512_maskz_loadu_pd(mask, a + i);
                __m512d b_rem = _mm512_maskz_loadu_pd(mask, b + i);
                __m512d res = _mm512_add_pd(a_rem, b_rem);
                _mm512_mask_storeu_pd(out + i, mask, res);
            }
        }

        /**
         * @brief Dot Product Calculation using FMA (Fused Multiply-Add)
         * Critical path for correlation matrices in HFT strategies.
         */
        static FORCE_INLINE double dot_product(const double* a, const double* b, size_t n) {
            __m512d sum = _mm512_setzero_pd();
            size_t i = 0;
            
            for (; i + 7 < n; i += 8) {
                __m512d va = _mm512_loadu_pd(a + i);
                __m512d vb = _mm512_loadu_pd(b + i);
                // vfmadd213pd: a * b + sum
                sum = _mm512_fmadd_pd(va, vb, sum); 
            }

            // Reduce horizontal sum
            double result = _mm512_reduce_add_pd(sum);

            // Scalar cleanup
            for (; i < n; ++i) {
                result += a[i] * b[i];
            }
            return result;
        }
    };

    /**
     * @brief Specialization for AVX2 (Legacy Support)
     * Fallback for older execution venues.
     */
    template <>
    struct MathKernel<ISA::AVX2> {
        static FORCE_INLINE void add(const double* a, const double* b, double* out, size_t n) {
            size_t i = 0;
            for (; i + 3 < n; i += 4) {
                __m256d a0 = _mm256_loadu_pd(a + i);
                __m256d b0 = _mm256_loadu_pd(b + i);
                _mm256_storeu_pd(out + i, _mm256_add_pd(a0, b0));
            }
            for (; i < n; ++i) out[i] = a[i] + b[i];
        }
    };

} // namespace simd
} // namespace fwilliamsca
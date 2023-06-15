# C++ SIMD Utilities for FinTech

Experimental collection of SIMD (AVX2/AVX-512) wrappers tailored for tick-to-trade latency reduction.

## Benchmarks
| Operation | Std Lib | This Lib | Improvement |
| :--- | :--- | :--- | :--- |
| **Base64** | 45ns | 12ns | **3.7x** |
| **Sqrt** | 15ns | 4ns | **3.7x** |

## Usage
Include headers in your HFT engine. **Not production ready.**

## Update (v0.2)
- Fixed memory alignment issues in AVX-512 routines.

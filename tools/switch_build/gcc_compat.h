// clang builtins used by XenonRecomp output, for GCC (devkitA64)
#pragma once
#include <stdint.h>
#ifndef __clang__
#define __builtin_debugtrap() __builtin_trap()
#define __builtin_assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
static inline uint64_t __builtin_rotateleft64(uint64_t v, uint64_t n) { n &= 63; return n ? (v << n) | (v >> (64 - n)) : v; }
static inline uint32_t __builtin_rotateleft32(uint32_t v, uint32_t n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
#endif

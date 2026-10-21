// Host tools build game/src/mgrr_shaders.cpp without the SDK runtime: its log macros print to stderr.
#pragma once
#include <cstdio>
#include <fmt/format.h>
#define REXLOG_INFO(...) std::fprintf(stderr, "%s\n", fmt::format(__VA_ARGS__).c_str())
#define REXLOG_WARN(...) std::fprintf(stderr, "warning: %s\n", fmt::format(__VA_ARGS__).c_str())

// SPDX-FileCopyrightText: 2026 Shinya Ishikawa
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdio>

namespace claptest {

inline int failures = 0;
inline int checks = 0;

inline void report(bool ok, const char* expr, const char* file, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "[FAIL] %s:%d CHECK(%s)\n", file, line, expr);
    }
}

#define CHECK(cond) ::claptest::report((cond), #cond, __FILE__, __LINE__)

inline int finish()
{
    if (failures == 0) {
        std::printf("[ OK ] clap_dance: %d checks passed\n", checks);
        return 0;
    }
    std::fprintf(stderr, "clap_dance: %d/%d checks FAILED\n", failures, checks);
    return 1;
}

} // namespace claptest

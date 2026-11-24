// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <cstdint>

namespace stackchan::conversation {

class ReplyWatchdog {
public:
    void restart(std::uint32_t now) { started_at_ = now; }
    bool expired(std::uint32_t now) const { return now - started_at_ >= 15000; }

private:
    std::uint32_t started_at_{};
};

} // namespace stackchan::conversation

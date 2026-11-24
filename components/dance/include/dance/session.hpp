// SPDX-License-Identifier: BSL-1.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stackchan::dance {

enum class SessionReturn { Standby, Listening, ToolResult, Reconnect };

inline SessionReturn session_return(bool online_at_entry, bool connected, bool tool_call)
{
    if (!online_at_entry) return SessionReturn::Standby;
    if (!connected) return SessionReturn::Reconnect;
    return tool_call ? SessionReturn::ToolResult : SessionReturn::Listening;
}

inline bool needs_busy_reply(std::string_view active_call_id, std::string_view incoming_call_id)
{
    return !incoming_call_id.empty() && active_call_id != incoming_call_id;
}

enum class SessionExit { HeadStroke, Timeout, Error };

inline const char* to_string(SessionExit reason) {
    switch (reason) {
    case SessionExit::HeadStroke: return "head_stroke";
    case SessionExit::Timeout: return "timeout";
    case SessionExit::Error: return "error";
    }
    return "error";
}

struct Session {
    explicit Session(std::uint32_t now = 0) : last_activity_ms{now} {}

    std::uint32_t last_activity_ms;
    std::vector<unsigned> tempos_bpm;

    void completed(unsigned bpm) { tempos_bpm.push_back(bpm); }
    bool expired(std::uint32_t now) const { return now - last_activity_ms >= 20000; }

    std::string summary(SessionExit reason) const {
        std::string tempos;
        for (const auto bpm : tempos_bpm) {
            if (!tempos.empty()) tempos += ',';
            tempos += std::to_string(bpm);
        }
        return std::string("{\"ok\":") + (reason == SessionExit::Error ? "false" : "true") +
               ",\"dance_count\":" + std::to_string(tempos_bpm.size()) +
               ",\"tempos_bpm\":[" + tempos +
               "],\"exit_reason\":\"" + to_string(reason) + "\"}";
    }
};

inline bool valid_session_arguments(std::string_view json) {
    auto i = json.find_first_not_of(" \t\r\n");
    if (i == json.npos || json[i] != '{') return false;
    i = json.find_first_not_of(" \t\r\n", i + 1);
    return i != json.npos && json[i] == '}' && json.find_first_not_of(" \t\r\n", i + 1) == json.npos;
}

}

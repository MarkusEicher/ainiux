#pragma once

#include <cstddef>
#include <string>

#include "common.hpp"

namespace ainiux::agent {

// Project-session totals for interactive `/statistics` (TUI and WebUI).
// Tokens are provider-reported when available and otherwise estimated.
struct AgentSessionStatistics {
    long long input_tokens = 0;
    long long cached_input_tokens = 0;
    long long fresh_input_tokens = 0;
    long long cache_write_tokens = 0;
    long long output_tokens = 0;
    std::size_t reported_rounds = 0;
    bool input_estimated = false;
    bool output_estimated = false;
    std::size_t tool_calls = 0;
    std::size_t failed_tool_calls = 0;
    long long elapsed_ms = 0;
};

// Always all three units, with singular when the count is 1:
// "1 hour, 2 minutes, 5 seconds".
std::string format_agent_elapsed_hms(long long elapsed_ms);

// Unicode-box two-column table for TUI notices (same printer as /show-index).
std::string format_agent_statistics_table(const AgentSessionStatistics& stats);

// Control-API JSON: numeric fields plus elapsed_text and markdown.
std::string format_agent_statistics_json(const AgentSessionStatistics& stats);

// Persist/restore a `statistics` object inside project settings_json.
Error statistics_from_settings_json(const std::string& settings_json,
                                    AgentSessionStatistics& stats);
Error settings_json_with_statistics(const std::string& settings_json,
                                    const AgentSessionStatistics& stats,
                                    std::string& updated);

}  // namespace ainiux::agent

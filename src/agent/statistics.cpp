#include "agent/statistics.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

#include "json/json.hpp"
#include "markdown/table_format.hpp"

namespace ainiux::agent {
namespace {

json::Value number_value(double number) {
    json::Value value;
    value.type = json::Value::Type::Number;
    value.number = number;
    return value;
}

json::Value bool_value(bool flag) {
    json::Value value;
    value.type = json::Value::Type::Bool;
    value.boolean = flag;
    return value;
}

json::Value object_value() {
    json::Value value;
    value.type = json::Value::Type::Object;
    return value;
}

std::string format_with_thousands(long long value) {
    const bool negative = value < 0;
    unsigned long long magnitude =
        static_cast<unsigned long long>(negative ? -value : value);
    const std::string digits = std::to_string(magnitude);
    std::string formatted;
    formatted.reserve(digits.size() + digits.size() / 3 + 1);
    if (negative) formatted.push_back('-');
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) formatted.push_back(',');
        formatted.push_back(digits[i]);
    }
    return formatted;
}

std::string format_percentage(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << '%';
    return out.str();
}

std::string json_int(long long value) { return std::to_string(value); }

std::string json_bool(bool value) { return value ? "true" : "false"; }

std::string json_percentage_number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

bool cache_hit_ratio(const AgentSessionStatistics& stats, double& percent) {
    const long long cached = stats.cached_input_tokens < 0 ? 0 : stats.cached_input_tokens;
    const long long fresh = stats.fresh_input_tokens < 0 ? 0 : stats.fresh_input_tokens;
    if (cached > std::numeric_limits<long long>::max() - fresh) return false;
    const long long total = cached + fresh;
    if (total <= 0) return false;
    percent = 100.0 * static_cast<double>(cached) / static_cast<double>(total);
    return true;
}

double fail_percentage(const AgentSessionStatistics& stats) {
    if (stats.tool_calls == 0) return 0.0;
    return 100.0 * static_cast<double>(stats.failed_tool_calls) /
           static_cast<double>(stats.tool_calls);
}

std::string unit_phrase(long long count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

Error read_nonneg_number(const json::Value& object,
                         const char* key,
                         long long& out) {
    const json::Value* value = object.get(key);
    if (value == nullptr || value->type == json::Value::Type::Null) return ok_error();
    if (value->type != json::Value::Type::Number || value->number < 0 ||
        value->number > 1.0e15)
        return {ErrorCode::Config,
                std::string("agent statistics ") + key +
                    " must be a non-negative integer"};
    out = static_cast<long long>(value->number);
    return ok_error();
}

Error read_bool_field(const json::Value& object, const char* key, bool& out) {
    const json::Value* value = object.get(key);
    if (value == nullptr || value->type == json::Value::Type::Null) return ok_error();
    if (value->type != json::Value::Type::Bool)
        return {ErrorCode::Config,
                std::string("agent statistics ") + key + " must be a boolean"};
    out = value->boolean;
    return ok_error();
}

}  // namespace

std::string format_agent_elapsed_hms(long long elapsed_ms) {
    if (elapsed_ms < 0) elapsed_ms = 0;
    const long long total_seconds = elapsed_ms / 1000;
    const long long hours = total_seconds / 3600;
    const long long minutes = (total_seconds % 3600) / 60;
    const long long seconds = total_seconds % 60;
    return unit_phrase(hours, "hour", "hours") + ", " +
           unit_phrase(minutes, "minute", "minutes") + ", " +
           unit_phrase(seconds, "second", "seconds");
}

std::string format_agent_statistics_table(const AgentSessionStatistics& stats) {
    double hit = 0.0;
    const std::string hit_text =
        cache_hit_ratio(stats, hit) ? format_percentage(hit) : std::string("n/a");
    std::string output = format_with_thousands(stats.output_tokens);
    if (stats.output_estimated) output += " (estimated)";
    const std::vector<std::string> headers{"Metric", "Value"};
    const std::vector<markdown::TableAlign> aligns{markdown::TableAlign::Left,
                                                   markdown::TableAlign::Right};
    const std::vector<std::vector<std::string>> rows{
        {"Cached input tokens", format_with_thousands(stats.cached_input_tokens)},
        {"Fresh input tokens", format_with_thousands(stats.fresh_input_tokens)},
        {"Output tokens", output},
        {"Cache hit ratio", hit_text},
        {"Tool calls", std::to_string(stats.tool_calls)},
        {"Failed tool calls", std::to_string(stats.failed_tool_calls)},
        {"Fail percentage", format_percentage(fail_percentage(stats))},
        {"Time spent", format_agent_elapsed_hms(stats.elapsed_ms)},
    };
    return markdown::format_table(headers, aligns, rows,
                                  markdown::TableStyle::PaddedGfm);
}

std::string format_agent_statistics_json(const AgentSessionStatistics& stats) {
    double hit = 0.0;
    const bool has_hit = cache_hit_ratio(stats, hit);
    std::string json = "{";
    json += "\"cached_input_tokens\":" + json_int(stats.cached_input_tokens);
    json += ",\"fresh_input_tokens\":" + json_int(stats.fresh_input_tokens);
    json += ",\"output_tokens\":" + json_int(stats.output_tokens);
    json += ",\"output_estimated\":" + json_bool(stats.output_estimated);
    json += ",\"cache_hit_ratio\":";
    json += has_hit ? json_percentage_number(hit) : std::string("null");
    json += ",\"tool_calls\":" + json_int(static_cast<long long>(stats.tool_calls));
    json += ",\"failed_tool_calls\":" +
            json_int(static_cast<long long>(stats.failed_tool_calls));
    json += ",\"fail_percentage\":" + json_percentage_number(fail_percentage(stats));
    json += ",\"elapsed_ms\":" + json_int(stats.elapsed_ms);
    json += ",\"elapsed_text\":" + json::quote(format_agent_elapsed_hms(stats.elapsed_ms));
    json += ",\"markdown\":" + json::quote(format_agent_statistics_table(stats));
    json += "}";
    return json;
}

Error statistics_from_settings_json(const std::string& settings_json,
                                    AgentSessionStatistics& stats) {
    stats = AgentSessionStatistics{};
    if (settings_json.empty() || settings_json == "{}") return ok_error();
    const json::ParseResult parsed = json::parse(settings_json);
    if (!parsed.error.ok() || !parsed.value.is_object())
        return {ErrorCode::Config, "agent project settings must be a JSON object"};
    const json::Value* value = parsed.value.get("statistics");
    if (value == nullptr || value->type == json::Value::Type::Null) return ok_error();
    if (!value->is_object())
        return {ErrorCode::Config, "agent statistics settings must be a JSON object"};
    long long number = 0;
    Error error = read_nonneg_number(*value, "input_tokens", number);
    if (!error.ok()) return error;
    stats.input_tokens = number;
    number = 0;
    error = read_nonneg_number(*value, "cached_input_tokens", number);
    if (!error.ok()) return error;
    stats.cached_input_tokens = number;
    number = 0;
    error = read_nonneg_number(*value, "fresh_input_tokens", number);
    if (!error.ok()) return error;
    stats.fresh_input_tokens = number;
    number = 0;
    error = read_nonneg_number(*value, "cache_write_tokens", number);
    if (!error.ok()) return error;
    stats.cache_write_tokens = number;
    number = 0;
    error = read_nonneg_number(*value, "output_tokens", number);
    if (!error.ok()) return error;
    stats.output_tokens = number;
    number = 0;
    error = read_nonneg_number(*value, "reported_rounds", number);
    if (!error.ok()) return error;
    stats.reported_rounds = static_cast<std::size_t>(number);
    error = read_bool_field(*value, "input_estimated", stats.input_estimated);
    if (!error.ok()) return error;
    error = read_bool_field(*value, "output_estimated", stats.output_estimated);
    if (!error.ok()) return error;
    number = 0;
    error = read_nonneg_number(*value, "tool_calls", number);
    if (!error.ok()) return error;
    stats.tool_calls = static_cast<std::size_t>(number);
    number = 0;
    error = read_nonneg_number(*value, "failed_tool_calls", number);
    if (!error.ok()) return error;
    stats.failed_tool_calls = static_cast<std::size_t>(number);
    number = 0;
    error = read_nonneg_number(*value, "elapsed_ms", number);
    if (!error.ok()) return error;
    stats.elapsed_ms = number;
    return ok_error();
}

Error settings_json_with_statistics(const std::string& settings_json,
                                    const AgentSessionStatistics& stats,
                                    std::string& updated) {
    json::Value root;
    if (settings_json.empty()) {
        root = object_value();
    } else {
        json::ParseResult parsed = json::parse(settings_json);
        if (!parsed.error.ok() || !parsed.value.is_object())
            return {ErrorCode::Config, "agent project settings must be a JSON object"};
        root = std::move(parsed.value);
    }
    json::Value object = object_value();
    object.object["input_tokens"] =
        number_value(static_cast<double>(stats.input_tokens));
    object.object["cached_input_tokens"] =
        number_value(static_cast<double>(stats.cached_input_tokens));
    object.object["fresh_input_tokens"] =
        number_value(static_cast<double>(stats.fresh_input_tokens));
    object.object["cache_write_tokens"] =
        number_value(static_cast<double>(stats.cache_write_tokens));
    object.object["output_tokens"] =
        number_value(static_cast<double>(stats.output_tokens));
    object.object["reported_rounds"] =
        number_value(static_cast<double>(stats.reported_rounds));
    object.object["input_estimated"] = bool_value(stats.input_estimated);
    object.object["output_estimated"] = bool_value(stats.output_estimated);
    object.object["tool_calls"] =
        number_value(static_cast<double>(stats.tool_calls));
    object.object["failed_tool_calls"] =
        number_value(static_cast<double>(stats.failed_tool_calls));
    object.object["elapsed_ms"] = number_value(static_cast<double>(stats.elapsed_ms));
    root.object["statistics"] = std::move(object);
    updated = json::stringify(root);
    return ok_error();
}

}  // namespace ainiux::agent

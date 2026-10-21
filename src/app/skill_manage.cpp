#include "app/app.hpp"

#include <algorithm>
#include <ostream>

#include "agent/skills.hpp"
#include "platform/environment.hpp"
#include "runtime/interrupt.hpp"
#include "security/redact.hpp"

namespace ainiux::app {
namespace {
json::Value string_value(const std::string& text) {
    json::Value result;
    result.type = json::Value::Type::String;
    result.string = text;
    return result;
}
}

bool skill_manage_requested(const cli::Options& options) {
    return options.list_skills || options.add_skill || options.remove_skill;
}

Error run_skill_manage(const cli::Options& options, std::ostream& out, std::ostream& err) {
    provider::RequestContext context;
    context.options = options;
    context.headers = options.headers;
    std::vector<std::string> secrets = request_secrets(context);
    const auto add_environment = [&](const std::string& name) {
        const std::string value = platform::environment_value(name.c_str());
        if (!value.empty()) secrets.push_back(value);
    };
    add_environment("AINIUX_API_KEY");
    if (!options.key_env.empty()) add_environment(options.key_env);
    for (const auto& profile : provider::built_in_profiles())
        for (const auto& name : profile.key_envs) add_environment(name);
    std::sort(secrets.begin(), secrets.end(), [](const std::string& left, const std::string& right) {
        return left.size() > right.size();
    });
    const auto redact = [&](const std::string& text) { return redact_secrets(text, secrets); };
    runtime::InterruptGuard interrupt;
    const auto interrupted = [&interrupt] { return interrupt.interrupted(); };
    if (options.add_skill) {
        std::string installed;
        Error error = agent::install_skill(options.skill_target, installed, {}, interrupted);
        if (error.ok() && !options.quiet) err << "Installed skill: " << redact(installed) << '\n';
        return error;
    }
    if (options.remove_skill) {
        Error error = agent::remove_skill(options.skill_target, {}, interrupted);
        if (error.ok() && !options.quiet) err << "Removed user skill: " << redact(options.skill_target) << '\n';
        return error;
    }
    agent::SkillCatalog catalog;
    Error error = agent::load_skills(".", true, {}, catalog, {}, interrupted);
    if (!error.ok()) return error;
    if (!options.quiet)
        for (const std::string& warning : catalog.warnings)
            err << "Skill warning: " << redact(warning) << '\n';
    json::Value entries;
    entries.type = json::Value::Type::Array;
    for (const agent::Skill& skill : catalog.skills) {
        json::Value item;
        item.type = json::Value::Type::Object;
        item.object["name"] = string_value(redact(skill.name));
        item.object["description"] = string_value(redact(skill.description));
        item.object["source"] = string_value(skill.source);
        item.object["path"] = string_value(redact(skill.directory));
        if (options.format == cli::OutputFormat::Ndjson) out << json::stringify(item) << '\n';
        else if (options.format == cli::OutputFormat::Text)
            out << redact(skill.name) << "  [" << skill.source << "]  "
                << redact(skill.directory) << '\n'
                << (skill.description.empty() ? "" : "  " + redact(skill.description) + "\n");
        entries.array.push_back(std::move(item));
    }
    if (options.format == cli::OutputFormat::Json) {
        json::Value root;
        root.type = json::Value::Type::Object;
        root.object["skills"] = std::move(entries);
        out << json::stringify(root) << '\n';
    } else if (options.format == cli::OutputFormat::Text && catalog.skills.empty()) {
        out << "No active skills. Install with: ainiux --add-skill FOLDER\n";
    }
    return ok_error();
}

}  // namespace ainiux::app

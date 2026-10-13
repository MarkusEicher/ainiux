#pragma once

#include <algorithm>
#include <string>

namespace ainiux::agent {

// Project-local state directory (index, agent.sqlite, history, logs).
// Deliberately distinct from the user profile dir ~/.ainiux/ (chat DB, media).
inline constexpr const char kProjectStateDirName[] = ".ainiux-pr";

inline std::string normalized_protected_name(std::string name) {
#if defined(_WIN32)
    std::transform(name.begin(), name.end(), name.begin(), [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    });
#endif
    return name;
}

inline bool is_project_state_dir_name(const std::string& name) {
    return normalized_protected_name(name) == kProjectStateDirName;
}

// Native mutations (write/edit/mkdir/mv/rm/apply_patch) refuse these in-project
// directory names. Reads and `run git` remain allowed. .ainiux-pr is also
// refused for reads via is_project_state_dir_name().
inline bool is_protected_mutation_dir_name(const std::string& name) {
    const std::string compared = normalized_protected_name(name);
    return compared == kProjectStateDirName || compared == ".git";
}

// Discovery exclusions and protected external metadata. Inside the active project,
// Agent access protects only is_project_state_dir_name(); VCS/profile-looking
// directories are ordinary project files, not a permission boundary.
inline bool is_protected_state_dir_name(const std::string& name) {
    const std::string compared = normalized_protected_name(name);
    return compared == kProjectStateDirName || compared == ".ainiux" || compared == ".git" ||
           compared == ".hg" || compared == ".svn";
}

}  // namespace ainiux::agent

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "common.hpp"
#include "runtime/runtime.hpp"

namespace ainiux::agent {

struct Skill {
    std::string name;
    std::string description;
    std::string directory;
    std::string source;  // user, project, explicit
    std::string instructions;  // Retained only for explicit --load-skill.
};

struct SkillCatalog {
    std::vector<Skill> skills;
    std::vector<std::string> warnings;
    std::string injection_text() const;
};

inline constexpr std::size_t kMaxSkillFileBytes = 262144;

Error load_skills(const std::string& workspace, bool automatic,
                  const std::vector<std::string>& explicit_paths, SkillCatalog& catalog,
                  runtime::CancellationToken cancellation = {},
                  std::function<bool()> interrupted = {});
Error read_skill_file(const SkillCatalog& catalog, const std::string& name,
                      const std::string& relative_path, std::string& content,
                      runtime::CancellationToken cancellation = {});
Error install_skill(const std::string& folder, std::string& installed_path,
                    runtime::CancellationToken cancellation = {},
                    std::function<bool()> interrupted = {});
Error remove_skill(const std::string& name,
                   runtime::CancellationToken cancellation = {},
                   std::function<bool()> interrupted = {});

}  // namespace ainiux::agent

#pragma once

#include <string>
#include <vector>

namespace ainiux::agent {

// A conservative, complete-argv assessment used by restricted command policy and by
// path validation. `path_operands` contains every filename/directory input
// recognized by the accepted invocation, including auxiliary input files.
// Unknown commands, options, or invocation shapes are not vetted.
struct ReadOnlyCommandAssessment {
    bool vetted = false;
    std::vector<std::string> path_operands;
    // git -C directories: real cwd-like paths that must stay in-project and
    // outside agent state. Distinct from pathspecs, which are git queries.
    std::vector<std::string> chdir_operands;
    // When true, path_operands are git pathspecs (check-ignore, status, diff,
    // ls-files, log -- PATH). They may name ignored dirs, .git, .ainiux-pr,
    // missing files, and in-tree `..`.
    bool git_query_pathspecs = false;
    std::string reason;
};

ReadOnlyCommandAssessment assess_read_only_command(
    const std::vector<std::string>& arguments);

// Agent command scope is independent of restricted/read-only allowlists. Unknown
// options do not reject a command. Known file-valued options are collected, while
// patterns, formats, messages, inline programs and Git revisions remain data.
struct CommandPathAssessment {
    std::vector<std::string> paths;
    std::vector<std::string> chdir_paths;
    std::vector<std::string> query_pathspecs;
};

CommandPathAssessment assess_agent_command_paths(
    const std::vector<std::string>& arguments);

// Index of the git porcelain subcommand after git(1) global options such as
// `-c name=value`, `-C path`, and pager flags. Returns arguments.size() when
// no subcommand is present. Shared by Smart classification, pager hardening,
// and Guard so `git -c core.pager=cat status` is still `status`.
std::size_t git_subcommand_index(const std::vector<std::string>& arguments);

// True when `git -c NAME=VALUE` can run an alias, hook, editor, credential
// helper, filter, or other external program. Agent git policy hard-denies
// those overrides.
bool git_config_assignment_is_dangerous(const std::string& assignment);

// Conservative `node --test` [paths] assessment. Executes project tests, so it
// is not part of RestrictedReadOnly. Broader node file/`-e` forms use
// assess_workspace_interpreter_command.
ReadOnlyCommandAssessment assess_node_test_command(
    const std::vector<std::string>& arguments);

// Conservative assessment for workspace-scoped interpreters and everyday
// C/C++/Java/C# toolchains: python file / -m / -c, node file / --test / -e,
// pytest, gcc/g++/clang/make/cmake/ninja, javac/java/jar/mvn/gradle, and
// dotnet/csc/msbuild. Not part of RestrictedReadOnly. Unknown python/node
// flags, listeners, inspect/watch/loaders, compiler plugins, install/publish
// targets, and inline payloads with out-of-workspace path literals stay
// unvetted. Smart uses explicit Guard risk rules, not this allowlist.
ReadOnlyCommandAssessment assess_workspace_interpreter_command(
    const std::vector<std::string>& arguments);

// Conservative argv classifier for in-project mkdir/rmdir/rm/mv. Unknown flags
// or shapes are not classified. Smart uses independent path/risk checks; a failed
// classification is never itself a reason to request approval.
struct WorkspaceFsCommandAssessment {
    bool classified = false;
    bool recursive_rm = false;
    std::vector<std::string> path_operands;
    std::string reason;
};

WorkspaceFsCommandAssessment assess_workspace_fs_command(
    const std::vector<std::string>& arguments);

}  // namespace ainiux::agent

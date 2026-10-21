#include "agent/test_skills.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "agent/prompts.hpp"
#include "agent/session_runtime.hpp"
#include "agent/skills.hpp"
#include "agent/tools.hpp"
#include "app/app.hpp"
#include "cli/args.hpp"
#include "platform/environment.hpp"
#include "platform/filesystem.hpp"
#include "support/test_support.hpp"

namespace ainiux::test::agent_skills {
namespace {
namespace fs = std::filesystem;
using ainiux::test::check;

struct Fixture {
    fs::path root;
    const std::optional<std::string> saved_home = test_environment("HOME");
    Fixture() {
        std::string suffix;
        check(platform::secure_random_hex(8, suffix).ok(), "skill test allocates unique temp name");
        root = fs::canonical(fs::temp_directory_path()) / ("ainiux-skills-" + suffix);
        fs::create_directories(root / "home");
        fs::create_directories(root / "project");
        set_test_environment("HOME", (root / "home").u8string());
    }
    ~Fixture() {
        if (saved_home) set_test_environment("HOME", *saved_home);
        else unset_test_environment("HOME");
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path user() const { return root / "home/.ainiux/skills"; }
    fs::path project() const { return root / "project"; }
    void write(const fs::path& path, const std::string& content) const {
        fs::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << content;
        check(static_cast<bool>(output), "write skill fixture");
    }
    std::string read(const fs::path& path) const {
        std::string text;
        check(platform::read_file_bounded(path.u8string(), 1024 * 1024, text).ok(), "read skill fixture");
        return text;
    }
};

bool ok(const std::string& text) {
    const auto parsed = json::parse(text);
    const auto* value = parsed.value.get("ok");
    return parsed.error.ok() && value && value->type == json::Value::Type::Bool && value->boolean;
}

void test_discovery_and_explicit_loads() {
    Fixture f;
    agent::SkillCatalog catalog;
    check(agent::load_skills(f.project().u8string(), true, {}, catalog).ok() && catalog.skills.empty(),
          "missing skill directories are normal");
    f.write(f.user() / "alpha/SKILL.md", "---\nname: ignored-name\ndescription: 'User''s alpha'\n---\nUSER RULE\n");
    f.write(f.user() / "beta/SKILL.md", "---\ndescription: \"Beta \\\"quoted\\\"\"\n---\nBETA RULE\n");
    f.write(f.project() / "skills/alpha/SKILL.md", "---\ndescription: Project alpha\n---\nPROJECT RULE\n");
    f.write(f.project() / "skills/plain/SKILL.md", "Plain Markdown instructions.\n");
    f.write(f.project() / "skills/ignored.md", "Not a skill directory.\n");
    f.write(f.project() / "skills/empty/SKILL.md", "  \n");
    f.write(f.project() / "skills/binary/SKILL.md", std::string("bad\0file", 8));
    f.write(f.project() / "skills/utf8/SKILL.md", std::string(1, static_cast<char>(0xff)));
    check(agent::load_skills(f.project().u8string(), true, {}, catalog).ok(), "load layered skills");
    check(catalog.skills.size() == 3 && catalog.skills[0].name == "alpha" &&
              catalog.skills[1].name == "beta" && catalog.skills[2].name == "plain",
          "effective skills are sorted, directory-named, and invalid entries skipped");
    check(catalog.skills[0].source == "project" && catalog.skills[0].description == "Project alpha" &&
              catalog.skills[1].source == "user" && catalog.skills[1].description == "Beta \"quoted\"",
          "project directory overrides user skill and metadata quotes parse");
    check(catalog.warnings.size() == 3, "invalid discovered skills report warnings");
    const std::string injection = catalog.injection_text();
    check(injection.find("PROJECT RULE") == std::string::npos &&
              injection.find("BETA RULE") == std::string::npos &&
              injection.find("skill tool") != std::string::npos,
          "automatic skills send a catalog and read-on-demand guidance, not instruction bodies");
    std::string text;
    check(agent::read_skill_file(catalog, "alpha", "", text).ok() && text.find("PROJECT RULE") != std::string::npos,
          "skill reads select the effective project copy");
    check(agent::load_skills(f.project().u8string(), false, {}, catalog).ok() && catalog.skills.empty(),
          "headless discovery is off even when both roots contain skills");
    f.write(f.root / "first/alpha/SKILL.md", "FIRST EXPLICIT\n");
    f.write(f.root / "last/alpha/SKILL.md", "LAST EXPLICIT\n");
    check(agent::load_skills(f.project().u8string(), true,
              {(f.root / "first/alpha").u8string(), (f.root / "last/alpha/SKILL.md").u8string()}, catalog).ok(),
          "explicit skill paths accept folders and SKILL.md");
    check(catalog.skills[0].source == "explicit" && catalog.skills[0].instructions == "LAST EXPLICIT\n" &&
              catalog.injection_text().find("FIRST EXPLICIT") == std::string::npos,
          "last explicit skill overrides automatic and earlier explicit copies");
    check(agent::load_skills(f.project().u8string(), false,
              {(f.root / "first/alpha").u8string()}, catalog).ok() && catalog.skills.size() == 1 &&
              catalog.injection_text().find("FIRST EXPLICIT") != std::string::npos,
          "headless explicit skills immediately include instructions and exclude automatic skills");
    check(!agent::load_skills(f.project().u8string(), false,
              {(f.root / "missing").u8string()}, catalog).ok(), "missing explicit skill fails");
    runtime::CancellationSource source;
    source.cancel();
    check(agent::load_skills(f.project().u8string(), true, {}, catalog, source.token()).code == ErrorCode::Cancelled,
          "skill discovery honours cancellation");
}

void test_skill_reads_and_permissions() {
    Fixture f;
    f.write(f.user() / "docs/SKILL.md", "Read references/guide.md. credential-value\n");
    f.write(f.user() / "docs/references/guide.md", "Guide text\n");
    f.write(f.user() / "docs/assets/binary", std::string("bad\0text", 8));
    f.write(f.user() / "docs/large.md", std::string(agent::kMaxSkillFileBytes + 1, 'a'));
    agent::SkillCatalog catalog;
    check(agent::load_skills(f.project().u8string(), true, {}, catalog).ok(), "discover user skill for reads");
    std::string text;
    check(agent::read_skill_file(catalog, "docs", "references/guide.md", text).ok() && text == "Guide text\n",
          "nested supporting Markdown is read from the skill root");
    for (const std::string path : {"../other.md", "/etc/passwd", "C:/file", "references/../../x", ".ainiux-pr/state", "x\\file"})
        check(!agent::read_skill_file(catalog, "docs", path, text).ok() && text.empty(),
              "skill reader rejects escaping/protected path " + path);
    check(!agent::read_skill_file(catalog, "unknown", "", text).ok(), "unknown skill cannot be read");
    check(!agent::read_skill_file(catalog, "docs", "assets/binary", text).ok(), "binary supporting file is rejected");
    check(!agent::read_skill_file(catalog, "docs", "large.md", text).ok(), "oversized supporting file is rejected");
#if !defined(_WIN32)
    fs::create_symlink(f.project(), f.user() / "docs/escape");
    check(!agent::read_skill_file(catalog, "docs", "escape/AGENTS.md", text).ok(),
          "supporting symlinks cannot escape selected skill roots");
    fs::create_directory_symlink(f.user() / "docs", f.project() / "skills-link");
    check(!agent::load_skills(f.project().u8string(), false,
              {(f.project() / "skills-link").u8string()}, catalog).ok(), "explicit linked root is rejected");
    check(agent::load_skills(f.project().u8string(), true, {}, catalog).ok(), "reload valid catalog");
#endif
    agent::ToolRegistryOptions options;
    options.mutation_policy = agent::MutationPolicy::Full;
    options.skills = catalog;
    options.permission_controls = true;
    options.permission_mode = agent::PermissionMode::Confirm;
    agent::ReadToolRegistry tools;
    check(agent::ReadToolRegistry::create_without_index(f.project().u8string(), {"credential-value"}, tools, options).ok(),
          "skill-aware registry prepares without a code index");
    bool advertised = false;
    for (const auto& definition : tools.definitions()) advertised |= definition.name == "skill";
    check(advertised, "active skill reader is advertised");
    const std::string result = tools.execute("skill", "{\"name\":\"docs\"}");
    check(ok(result) && result.find("credential-value") == std::string::npos && result.find("[REDACTED]") != std::string::npos,
          "scoped skill read is read-only in Confirm and redacts configured credentials");
    check(ok(tools.execute("skill", "{\"name\":\"docs\",\"path\":\"references/guide.md\"}")),
          "native skill arguments read supporting documents");
    check(!ok(tools.execute("skill", "{}")), "skill name is required");
    runtime::CancellationSource source;
    source.cancel();
    check(!ok(tools.execute("skill", "{\"name\":\"docs\"}", source.token())), "tool cancellation is respected");
    options.mutation_policy = agent::MutationPolicy::Disabled;
    agent::ReadToolRegistry review;
    check(agent::ReadToolRegistry::create_without_index(f.project().u8string(), {}, review, options).ok(), "prepare review registry");
    check(!ok(review.execute("skill", "{\"name\":\"docs\"}")), "security review cannot execute skill tool");
}

void test_install_replace_remove_and_cleanup() {
    Fixture f;
    const fs::path source = f.root / "source/demo";
    f.write(source / "SKILL.md", "ORIGINAL\n");
    f.write(source / "references/guide.md", "Guide\n");
    f.write(source / "agents/openai.yaml", "display_name: Demo\n");
    f.write(source / "assets/data.bin", std::string("\0\1\2", 3));
    std::string installed;
    check(agent::install_skill(source.u8string(), installed).ok(), "install complete skill directory");
    check(installed == (f.user() / "demo").u8string() &&
              f.read(f.user() / "demo/references/guide.md") == "Guide\n" &&
              f.read(f.user() / "demo/assets/data.bin") == std::string("\0\1\2", 3) &&
              fs::exists(f.user() / "demo/agents/openai.yaml"), "install preserves documents, metadata and binary assets");
    f.write(source / "SKILL.md", "REPLACEMENT\n");
    fs::remove(source / "references/guide.md");
    check(agent::install_skill(source.u8string(), installed).ok() &&
              f.read(f.user() / "demo/SKILL.md") == "REPLACEMENT\n" &&
              !fs::exists(f.user() / "demo/references/guide.md"), "update replaces rather than merges installed copy");
    check(!agent::install_skill((f.user() / "demo").u8string(), installed).ok() &&
              f.read(f.user() / "demo/SKILL.md") == "REPLACEMENT\n", "self installation is refused without deleting installed files");
    f.write(source / "SKILL.md", "CANCELLED UPDATE\n");
    check(agent::install_skill(source.u8string(), installed, {}, [&] {
              return fs::exists(f.user() / ".install-lock");
          }).code == ErrorCode::Cancelled && f.read(f.user() / "demo/SKILL.md") == "REPLACEMENT\n",
          "cancellation during staging preserves the previous installation");
    check(!fs::exists(f.user() / ".install-lock"), "cancellation releases installation lock");
    for (const auto& entry : fs::directory_iterator(f.user()))
        check(entry.path().filename() == "demo", "cancellation cleans all staging directories");
#if !defined(_WIN32)
    fs::create_symlink(f.project(), source / "unsafe");
    check(!agent::install_skill(source.u8string(), installed).ok() &&
              f.read(f.user() / "demo/SKILL.md") == "REPLACEMENT\n", "linked source fails and preserves the previous copy");
    fs::remove(source / "unsafe");
    check(::mkfifo((source / "pipe").c_str(), 0600) == 0, "create special-file skill fixture");
    check(!agent::install_skill(source.u8string(), installed).ok(), "special files fail without blocking reads");
    fs::remove(source / "pipe");
    const auto permissions = fs::status(f.user() / "demo/SKILL.md").permissions();
    check((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
          "installed skill files are private");
    check((fs::status(f.user()).permissions() & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
          "installation protects existing user skills directories");
#endif
    f.write(f.project() / "skills/demo/SKILL.md", "PROJECT COPY\n");
    check(!agent::remove_skill("../demo").ok() && !agent::remove_skill("demo/child").ok(), "remove accepts names only");
    runtime::CancellationSource cancel;
    cancel.cancel();
    check(agent::remove_skill("demo", cancel.token()).code == ErrorCode::Cancelled && fs::exists(f.user() / "demo"),
          "cancelled removal preserves installed directory");
    check(agent::remove_skill("demo").ok() && !fs::exists(f.user() / "demo") &&
              fs::exists(f.project() / "skills/demo/SKILL.md"), "removal touches user copy only");
    check(!agent::remove_skill("demo").ok(), "missing installed name is actionable error");
    check(!agent::remove_skill(".install-lock").ok(), "internal installation state cannot be removed as a skill");
}

void test_cli_and_seed() {
    Fixture f;
    f.write(f.project() / "skills/demo/SKILL.md", "EXPLICIT INSTRUCTIONS\n");
    const char* argv[] = {"ainiux", "run", "--load-skill", "one", "--load-skill=two", "-r", "task"};
    auto parsed = cli::parse_args(7, const_cast<char**>(argv));
    check(parsed.error.ok() && parsed.options.load_skills == std::vector<std::string>({"one", "two"}) &&
              cli::validate_agent_run_arguments(7, const_cast<char**>(argv), parsed.options).ok(), "repeatable skill paths parse in run");
    const char* agent_args[] = {"ainiux", "agent", "--load-skill", "one"};
    parsed = cli::parse_args(4, const_cast<char**>(agent_args));
    check(parsed.error.ok() && cli::validate_agent_interactive_arguments(4, const_cast<char**>(agent_args), parsed.options).ok(),
          "explicit skill paths parse in interactive agent");
    for (const std::vector<std::string>& arguments : {
             std::vector<std::string>{"ainiux", "--load-skill", "one"},
             {"ainiux", "--load-skill="}, {"ainiux", "--add-skill="},
             {"ainiux", "--list-skills", "--chat"}, {"ainiux", "--list-skills", "--list-mcp"},
             {"ainiux", "--add-skill", "one", "--remove-skill", "two"},
             {"ainiux", "--list-skills", "--server"}}) {
        std::vector<char*> raw;
        for (const auto& argument : arguments) raw.push_back(const_cast<char*>(argument.c_str()));
        check(!cli::parse_args(static_cast<int>(raw.size()), raw.data()).error.ok(), "invalid skill/mode combination is rejected");
    }
    cli::Options listing;
    listing.list_skills = true;
    listing.format = cli::OutputFormat::Json;
    std::ostringstream out, err;
    check(app::run_skill_manage(listing, out, err).ok() && json::parse(out.str()).value.get("skills"),
          "standalone skill listing emits parseable JSON without a provider");
    agent::SkillCatalog catalog;
    check(agent::load_skills(f.project().u8string(), false,
              {(f.project() / "skills/demo").u8string()}, catalog).ok(), "load explicit seed fixture");
    agent::TrustedPrompts prompts;
    prompts.agent = "STATIC AGENT";
    for (auto protocol : {agent::ToolProtocol::Native, agent::ToolProtocol::Xml}) {
        provider::ToolConversation conversation;
        agent::seed_agent_conversation(conversation, prompts, agent::AgentLane::Act, protocol,
                                       "USER GOAL", "PROJECT RULES", {}, catalog.injection_text());
        check(conversation.messages.size() == 5 && conversation.messages[0].content == prompts.agent_system_prompt(protocol) &&
                  conversation.messages[2].role == "user" && conversation.messages[2].content.find("EXPLICIT INSTRUCTIONS") != std::string::npos &&
                  conversation.messages.back().content == "USER GOAL", "native/XML seeds keep skills separate from stable system prompt and user goal");
    }
}

void test_runtime_surface_selection() {
    Fixture f;
    f.write(f.project() / "skills/demo/SKILL.md", "Runtime instructions\n");
    provider::RequestContext context;
    context.options.provider = "none";
    context.options.quiet = true;
    context.options.agent_log_enabled = false;
    context.options.disable_indexing = true;
    context.profile.name = "none";
    context.profile.offline = true;
    auto has_skills = [](const agent::AgentSessionRuntime& session) {
        for (const auto& notice : session.context_load_notices())
            if (notice.rfind("skills loaded", 0) == 0) return true;
        return false;
    };
    agent::AgentSessionRuntime session;
    for (bool interactive : {true, false}) {
        auto options = agent::make_session_runtime_options(context, f.project().u8string(), agent::AgentLane::Act, interactive);
        check(session.prepare(context, {}, {}, options).ok() && has_skills(session) == interactive,
              "shared terminal/WebUI runtime discovers skills only for interactive agent sessions");
        session.reset();
    }
    context.options.load_skills = {(f.project() / "skills/demo/SKILL.md").u8string()};
    auto options = agent::make_session_runtime_options(context, f.project().u8string(), agent::AgentLane::Act, false);
    check(options.load_skills == context.options.load_skills && session.prepare(context, {}, {}, options).ok() && has_skills(session),
          "headless runtime receives explicit skills through the common CLI projection");
    session.reset();
    options.load_skills = {(f.root / "missing").u8string()};
    check(!session.prepare(context, {}, {}, options).ok() && !session.prepared(), "invalid explicit skills leave runtime unprepared");
    session.reset();
}
}  // namespace

void run_all() {
    test_discovery_and_explicit_loads();
    test_skill_reads_and_permissions();
    test_install_replace_remove_and_cleanup();
    test_cli_and_seed();
    test_runtime_surface_selection();
}
}  // namespace ainiux::test::agent_skills

#include "../src/runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static int g_fails = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

static std::string testsDir() {
    std::string       file     = __FILE__;
    const auto        slash    = file.find_last_of('/');
    const std::string dir      = (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
    return dir;
}

static std::string repoRoot() {
    return std::filesystem::weakly_canonical(testsDir() + "/..").string();
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void writeFile(const std::string& path, const std::string& body) {
    std::ofstream out(path);
    out << body;
}

static std::string functionBody(const std::string& src, const std::string& signature) {
    const auto pos = src.find(signature);
    if (pos == std::string::npos)
        return {};
    const auto brace = src.find('{', pos);
    if (brace == std::string::npos)
        return {};
    int depth = 0;
    for (size_t i = brace; i < src.size(); ++i) {
        if (src[i] == '{')
            depth++;
        else if (src[i] == '}') {
            depth--;
            if (depth == 0)
                return src.substr(brace, i - brace + 1);
        }
    }
    return {};
}

static int countNeedle(const std::string& hay, const std::string& needle) {
    int    n = 0;
    size_t p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) {
        n++;
        p += needle.size();
    }
    return n;
}

static int runCmd(const std::string& cmd, std::string& output) {
    output.clear();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return 127;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe))
        output += buf;
    const int st = pclose(pipe);
    if (st == -1)
        return 127;
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return 1;
}

static const char* kStatePath = "/tmp/hypr-shiny-border.lastso";

struct StateGuard {
    bool        existed = false;
    std::string orig;

    StateGuard() {
        existed = std::filesystem::exists(kStatePath);
        if (existed)
            orig = readFile(kStatePath);
    }

    ~StateGuard() {
        if (existed)
            writeFile(kStatePath, orig);
        else
            std::filesystem::remove(kStatePath);
    }
};

struct StubHyprctl {
    std::string dir;
    std::string record;
    std::string listFile;
    std::string instancesFile;
    std::string pluginctl;

    bool init() {
        std::string tmpl = "/tmp/shiny-reload-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* made = mkdtemp(buf.data());
        if (!made)
            return false;
        dir            = made;
        record         = dir + "/record";
        listFile       = dir + "/plugin_list";
        instancesFile  = dir + "/instances.json";
        pluginctl      = repoRoot() + "/scripts/pluginctl.sh";
        writeFile(record, "");
        writeFile(listFile, "no plugins loaded\n");
        writeFile(instancesFile, R"([
  {"instance": "live", "time": 1, "pid": 1, "wl_socket": "wayland-0"},
  {"instance": "nest", "time": 2, "pid": 2, "wl_socket": "wayland-1"}
]
)");
        const std::string stubPath = dir + "/hyprctl";
        writeFile(stubPath, R"STUB(#!/usr/bin/env bash
set -euo pipefail
log="${HYPRCTL_RECORD:?}"
printf '%s\n' "$*" >>"$log"

args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -j) shift ;;
    -i|--instance) shift; shift ;;
    *) args+=("$1"); shift ;;
  esac
done

if [[ ${#args[@]} -ge 1 && "${args[0]}" == "instances" ]]; then
  cat "${HYPRCTL_INSTANCES:?}"
  exit 0
fi
if [[ ${#args[@]} -ge 2 && "${args[0]}" == "plugin" && "${args[1]}" == "list" ]]; then
  cat "${HYPRCTL_PLUGIN_LIST:?}"
  exit 0
fi
if [[ ${#args[@]} -ge 2 && "${args[0]}" == "plugin" && "${args[1]}" == "load" ]]; then
  exit 0
fi
if [[ ${#args[@]} -ge 2 && "${args[0]}" == "plugin" && "${args[1]}" == "unload" ]]; then
  if [[ "${HYPRCTL_UNLOAD_FAIL:-}" == "1" ]]; then
    exit 1
  fi
  exit 0
fi
exit 0
)STUB");
        if (::chmod(stubPath.c_str(), 0755) != 0)
            return false;
        return std::filesystem::exists(pluginctl);
    }

    ~StubHyprctl() {
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }

    void setList(const std::string& body) { writeFile(listFile, body); }
    void clearRecord() { writeFile(record, ""); }
    std::string recorded() const { return readFile(record); }

    int run(const std::string& subcmd, const std::string& extraEnv, std::string& output) {
        const std::string cmd = "env -u SHINY_LIVE -u SHINY_INSTANCE " + extraEnv + " PATH=" + dir +
                                ":\"$PATH\" HYPRCTL_RECORD='" + record + "' HYPRCTL_PLUGIN_LIST='" + listFile +
                                "' HYPRCTL_INSTANCES='" + instancesFile + "' '" + pluginctl + "' " + subcmd + " 2>&1";
        return runCmd(cmd, output);
    }
};

static void checkResolvedBorderSize() {
    // plugin value >= 0 wins, including 0 (no ring).
    CHECK(shinyResolvedBorderSize(3, 2) == 3);
    CHECK(shinyResolvedBorderSize(0, 7) == 0);
    CHECK(shinyResolvedBorderSize(20, 1) == 20);
    // -1 follows general:border_size.
    CHECK(shinyResolvedBorderSize(-1, 2) == 2);
    CHECK(shinyResolvedBorderSize(-1, 0) == 0);
    CHECK(shinyResolvedBorderSize(-1, 8) == 8);
}

static void checkProductionWiring() {
    const std::string src     = repoRoot() + "/src";
    const std::string deco    = readFile(src + "/deco.cpp");
    const std::string main    = readFile(src + "/main.cpp");
    const std::string globals = readFile(src + "/globals.hpp");
    const std::string script  = readFile(repoRoot() + "/scripts/pluginctl.sh");

    CHECK(!deco.empty());
    CHECK(!main.empty());
    CHECK(!globals.empty());
    CHECK(!script.empty());

    const auto border = functionBody(deco, "CShinyBorder::borderSize");
    CHECK(!border.empty());
    CHECK(border.find("shinyResolvedBorderSize") != std::string::npos);
    CHECK(border.find("CConfigValue") == std::string::npos);
    CHECK(border.find("static ") == std::string::npos);
    CHECK(deco.find("config/ConfigValue.hpp") == std::string::npos);
    CHECK(deco.find("PBORDERSIZE") == std::string::npos);
    CHECK(deco.find("static auto PBORDERSIZE") == std::string::npos);
    CHECK(deco.find("CConfigValue<Config::INTEGER>") == std::string::npos);

    CHECK(globals.find("std::optional<CConfigValue<Config::INTEGER>>") != std::string::npos);
    CHECK(globals.find("generalBorderSize") != std::string::npos);
    CHECK(globals.find("kGeneralBorderSizeKey") != std::string::npos);
    CHECK(globals.find("general:border_size") != std::string::npos);

    const auto init = functionBody(main, "PLUGIN_DESCRIPTION_INFO PLUGIN_INIT");
    const auto exit = functionBody(main, "void PLUGIN_EXIT");
    CHECK(!init.empty());
    CHECK(!exit.empty());
    CHECK(init.find("generalBorderSize.emplace") != std::string::npos);
    CHECK(init.find("kGeneralBorderSizeKey") != std::string::npos);
    CHECK(exit.find("generalBorderSize.reset()") != std::string::npos);
    CHECK(main.find("static auto PBORDERSIZE") == std::string::npos);

    const auto loadAt   = script.find("\n  load)");
    const auto unloadAt = script.find("\n  unload)");
    const auto reloadAt = script.find("\n  reload)");
    const auto starAt   = script.find("\n  *)");
    CHECK(loadAt != std::string::npos && unloadAt != std::string::npos);
    CHECK(reloadAt != std::string::npos && starAt != std::string::npos);
    CHECK(loadAt < unloadAt);
    CHECK(unloadAt < reloadAt);
    CHECK(reloadAt < starAt);

    const auto load = script.substr(loadAt, unloadAt - loadAt);
    CHECK(load.find("already_loaded_by_name") != std::string::npos);
    CHECK(load.find("plugin load") != std::string::npos);
    CHECK(load.find("/tmp/hypr-shiny-border-$$.so") != std::string::npos);
    const auto nameCheck = load.find("already_loaded_by_name");
    const auto copyAt    = load.find("/tmp/hypr-shiny-border-$$.so");
    const auto loadCmd   = load.find("plugin load");
    CHECK(nameCheck < copyAt);
    CHECK(copyAt < loadCmd);

    const auto helper = script.substr(0, loadAt);
    CHECK(helper.find("already_loaded_by_name") != std::string::npos);
    const auto helperList = helper.find("plugin list");
    CHECK(helperList != std::string::npos);
    CHECK(helper.find("PLUGIN_NAME") != std::string::npos);

    const auto reload = script.substr(reloadAt, starAt - reloadAt);
    const auto uCall  = reload.find("\"$0\" unload");
    const auto lCall  = reload.find("\"$0\" load");
    CHECK(uCall != std::string::npos && lCall != std::string::npos);
    CHECK(uCall < lCall);

    CHECK(script.find("SHINY_INSTANCE") != std::string::npos);
    CHECK(script.find("SHINY_LIVE") != std::string::npos);
    CHECK(script.find("refuse_live") != std::string::npos);
}

static void checkPluginctl() {
    StateGuard     state;
    StubHyprctl    stub;
    CHECK(stub.init());
    if (stub.dir.empty())
        return;

    // Drive the real script, not a copy.
    CHECK(stub.pluginctl == repoRoot() + "/scripts/pluginctl.sh");
    CHECK(std::filesystem::exists(stub.pluginctl));

    const std::string so = repoRoot() + "/hypr-shiny-border.so";
    CHECK(std::filesystem::exists(so));

    std::string out;

    // Name already listed (any path) → non-zero, no plugin load, no STATE write.
    writeFile(kStatePath, "KEEP\n");
    stub.clearRecord();
    stub.setList("Plugin hypr-shiny-border by wmfeht:\n\tHandle: 0x1\n\tVersion: 0.1.0\n"
                 "\tDescription: Gradient window border that faces the cursor\n");
    int rc = stub.run("load", "", out);
    CHECK(rc != 0);
    CHECK(out.find("already loaded") != std::string::npos);
    CHECK(countNeedle(stub.recorded(), "plugin load") == 0);
    CHECK(readFile(kStatePath) == "KEEP\n");

    // Listed only via a different path still refuses.
    stub.clearRecord();
    stub.setList("loaded: /var/lib/hypr/hypr-shiny-border.so\n");
    rc = stub.run("load", "", out);
    CHECK(rc != 0);
    CHECK(countNeedle(stub.recorded(), "plugin load") == 0);

    // Name not listed → copy under /tmp/hypr-shiny-border-*.so, exactly one plugin load.
    stub.clearRecord();
    stub.setList("Plugin hyprbars by Vaxry:\n\tHandle: 0x2\n\tVersion: 1.0\n\tDescription: bars\n");
    rc = stub.run("load", "", out);
    CHECK(rc == 0);
    CHECK(countNeedle(stub.recorded(), "plugin load") == 1);
    const auto rec  = stub.recorded();
    const auto loadAt = rec.find("plugin load ");
    CHECK(loadAt != std::string::npos);
    auto destBegin = loadAt + std::strlen("plugin load ");
    auto destEnd   = rec.find('\n', destBegin);
    const std::string dest = rec.substr(destBegin, destEnd == std::string::npos ? std::string::npos : destEnd - destBegin);
    CHECK(dest.find("/tmp/hypr-shiny-border-") == 0);
    CHECK(dest.size() > std::strlen("/tmp/hypr-shiny-border-.so"));
    CHECK(dest.ends_with(".so"));
    CHECK(std::filesystem::exists(dest));
    CHECK(std::filesystem::file_size(dest) == std::filesystem::file_size(so));
    CHECK(readFile(kStatePath).find("/tmp/hypr-shiny-border-") != std::string::npos);
    std::filesystem::remove(dest);

    // reload is unload then load.
    writeFile(kStatePath, "/tmp/shiny-old-copy.so\n");
    stub.clearRecord();
    stub.setList("no plugins loaded\n");
    rc = stub.run("reload", "", out);
    CHECK(rc == 0);
    const auto rec2 = stub.recorded();
    CHECK(countNeedle(rec2, "plugin unload") == 1);
    CHECK(countNeedle(rec2, "plugin load") == 1);
    CHECK(rec2.find("plugin unload") < rec2.find("plugin load"));
    CHECK(rec2.find("/tmp/shiny-old-copy.so") != std::string::npos);
    const auto load2 = rec2.find("plugin load ");
    CHECK(load2 != std::string::npos);
    auto d2b = load2 + std::strlen("plugin load ");
    auto d2e = rec2.find('\n', d2b);
    const std::string dest2 = rec2.substr(d2b, d2e == std::string::npos ? std::string::npos : d2e - d2b);
    if (std::filesystem::exists(dest2))
        std::filesystem::remove(dest2);

    // Unload leaves the name listed → following load still refuses, no second copy.
    writeFile(kStatePath, "/tmp/shiny-old-copy.so\n");
    stub.clearRecord();
    stub.setList("Plugin hypr-shiny-border by wmfeht:\n\tHandle: 0x1\n");
    rc = stub.run("reload", "HYPRCTL_UNLOAD_FAIL=1", out);
    CHECK(rc != 0);
    const auto rec3 = stub.recorded();
    CHECK(countNeedle(rec3, "plugin unload") == 1);
    CHECK(countNeedle(rec3, "plugin load") == 0);
    CHECK(out.find("already loaded") != std::string::npos);

    // Instance 0 without SHINY_LIVE=1 still refuses. No plugin load.
    stub.clearRecord();
    stub.setList("no plugins loaded\n");
    rc = stub.run("load", "SHINY_INSTANCE=0", out);
    CHECK(rc != 0);
    CHECK(out.find("refusing to touch the live Hyprland session") != std::string::npos);
    CHECK(countNeedle(stub.recorded(), "plugin load") == 0);
    CHECK(stub.recorded().empty());
}

int main() {
    checkResolvedBorderSize();
    checkProductionWiring();
    checkPluginctl();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

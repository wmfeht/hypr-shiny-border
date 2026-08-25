#include "../src/teardown.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int g_fails = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

static std::string sourceDir() {
    std::string file = __FILE__;
    const auto  slash = file.find_last_of('/');
    const std::string testsDir = (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
    return testsDir + "/../src";
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static int  g_compiles     = 0;
static int  g_resets       = 0;
static int  g_makeCurrent  = 0;
static bool g_glAlive      = false;
static bool g_shaderLive   = false;

static bool spyGlAlive() {
    return g_glAlive;
}

static void spyMakeCurrent() {
    g_makeCurrent++;
}

static bool spyShaderLive() {
    return g_shaderLive;
}

static bool spyCompile() {
    g_compiles++;
    g_shaderLive = true;
    return true;
}

static void spyReset() {
    g_resets++;
    g_shaderLive = false;
}

static void bindSpies() {
    shinySetShaderOps({
        .glAlive     = spyGlAlive,
        .makeCurrent = spyMakeCurrent,
        .shaderLive  = spyShaderLive,
        .compile     = spyCompile,
        .reset       = spyReset,
    });
}

static void checkProductionWiring() {
    const std::string src  = sourceDir();
    const std::string pass = readFile(src + "/pass.cpp");
    const std::string main = readFile(src + "/main.cpp");
    const std::string readme = readFile(src + "/../README.md");

    CHECK(!pass.empty());
    CHECK(!main.empty());
    CHECK(!readme.empty());

    // Production must bind the Hyprland compile/reset into the shipped lifecycle.
    CHECK(pass.find("shinySetShaderOps") != std::string::npos);
    CHECK(pass.find("createProgram") != std::string::npos);
    CHECK(pass.find("hyprResetShader") != std::string::npos);
    CHECK(pass.find("g_shinyShader.reset()") != std::string::npos);

    // PLUGIN_EXIT: mark, leftover-pass clear, then destroy. No surgical shiny remove.
    CHECK(main.find("markShinyTeardown") != std::string::npos);
    CHECK(main.find("m_renderPass.clear()") != std::string::npos);
    CHECK(main.find("destroyShinyShader") != std::string::npos);
    CHECK(main.find(".removeAllOfType(\"CShinyPassElement\")") == std::string::npos);
    CHECK(main.find(".removeAllOfType(\"CBorderPassElement\")") == std::string::npos);

    const auto markAt  = main.find("markShinyTeardown");
    const auto clearAt = main.find("m_renderPass.clear()");
    const auto destAt  = main.find("destroyShinyShader();");
    CHECK(markAt != std::string::npos && clearAt != std::string::npos && destAt != std::string::npos);
    CHECK(markAt < clearAt);
    CHECK(clearAt < destAt);

    CHECK(readme.find("m_renderPass.clear()") != std::string::npos);
}

int main() {
    bindSpies();

    CHECK(!shinyTeardownStarted());

    // Missing shader, GL alive → compile once.
    g_glAlive    = true;
    g_shaderLive = false;
    CHECK(ensureShinyShader() == true);
    CHECK(g_compiles == 1);
    CHECK(g_shaderLive);

    // Already live → no second compile.
    CHECK(ensureShinyShader() == true);
    CHECK(g_compiles == 1);

    // Issue 5: leftover draw after PLUGIN_EXIT must not compile into a dying .so.
    markShinyTeardown();
    CHECK(shinyTeardownStarted());

    g_shaderLive = false;
    CHECK(ensureShinyShader() == false);
    CHECK(g_compiles == 1);

    g_shaderLive = true;
    CHECK(ensureShinyShader() == false);
    CHECK(g_compiles == 1);

    // Issue 2: no GL context → leak the SP (do not reset / glDelete).
    const int makeBefore = g_makeCurrent;
    g_glAlive            = false;
    destroyShinyShader();
    CHECK(g_resets == 0);
    CHECK(g_makeCurrent == makeBefore);

    // GL alive → make current, then reset.
    g_glAlive = true;
    destroyShinyShader();
    CHECK(g_resets == 1);
    CHECK(g_makeCurrent == makeBefore + 1);
    CHECK(!g_shaderLive);

    checkProductionWiring();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

#include "../src/runtime.hpp"

#include <cmath>
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
    std::string file           = __FILE__;
    const auto  slash          = file.find_last_of('/');
    const std::string testsDir = (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
    return testsDir + "/../src";
}

static std::string repoRoot() {
    return sourceDir() + "/..";
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

static void checkDrawAgreement() {
    const ShinyDrawShared p{
        .rounding      = 12,
        .outerRound    = 18,
        .roundingPower = 2.25f,
        .a             = 0.6f,
        .borderSize    = 3,
        .colA          = 0xFF112233ULL,
        .colB          = 0xFF445566ULL,
    };

    const float scales[] = {1.f, 2.f};
    for (const float scale : scales) {
        const auto mapped = shinyMapDrawBackends(p, scale);

        CHECK(mapped.shader.rounding == mapped.fallback.shared.rounding);
        CHECK(mapped.shader.outerRound == mapped.fallback.shared.outerRound);
        CHECK(mapped.shader.roundingPower == mapped.fallback.shared.roundingPower);
        CHECK(mapped.shader.a == mapped.fallback.shared.a);
        CHECK(mapped.shader.colA == mapped.fallback.shared.colA);
        CHECK(mapped.shader.colB == mapped.fallback.shared.colB);

        CHECK(mapped.shader.rounding == p.rounding);
        CHECK(mapped.shader.outerRound == p.outerRound);
        CHECK(mapped.shader.roundingPower == p.roundingPower);
        CHECK(mapped.shader.a == p.a);
        CHECK(mapped.shader.colA == p.colA);
        CHECK(mapped.shader.colB == p.colB);

        CHECK(mapped.shader.borderSize == 3);
        CHECK(mapped.fallback.shared.borderSize == 3);
        CHECK(mapped.shader.borderSize == p.borderSize);
        CHECK(mapped.fallback.shared.borderSize == p.borderSize);
        CHECK(mapped.shader.borderSize != 6);
        CHECK(mapped.shader.borderSize != 12);
        CHECK(mapped.fallback.shared.borderSize != 6);
        CHECK(mapped.fallback.shared.borderSize != 12);

        CHECK(mapped.fallback.expandPx == shinyFallbackExpandPx(p.borderSize, scale));
        if (scale == 1.f) {
            CHECK(mapped.fallback.expandPx == 3);
        } else {
            CHECK(mapped.fallback.expandPx == 6);
            CHECK(mapped.fallback.expandPx != 12);
            CHECK(mapped.fallback.expandPx != mapped.fallback.shared.borderSize);
        }
    }

    CHECK(shinyFallbackExpandPx(3, 1.f) == 3);
    CHECK(shinyFallbackExpandPx(3, 2.f) == 6);
    CHECK(shinyFallbackExpandPx(3, 2.f) != 12);
}

static void checkThickness() {
    // Contract: logical px × scale once. 3px @ 1× is 3 framebuffer px; @ 2× is 6, not 12.
    CHECK(shinyShaderThick(3.f, 1.f) == 3.f);
    CHECK(shinyShaderThick(3.f, 2.f) == 6.f);
    CHECK(shinyShaderThick(3.f, 2.f) != 12.f);
    CHECK(shinyShaderThick(0.f, 2.f) == 0.f);
}

static void checkHeading() {
    // Pointer at the transformed origin (0,0) still uses atan vs center — no CPU-angle fallback.
    const float origin = shinyGpuHeading(0.f, 0.f, 100.f, 100.f);
    const float onRay  = shinyGpuHeading(50.f, 50.f, 100.f, 100.f);
    CHECK(std::isfinite(origin));
    CHECK(std::isfinite(onRay));
    CHECK(std::fabs(origin - onRay) < 1e-5f);

    const float atCenter = shinyGpuHeading(100.f, 100.f, 100.f, 100.f);
    CHECK(std::isfinite(atCenter));

    // Two pointers on the same ray from center share a heading.
    const float a = shinyGpuHeading(110.f, 100.f, 100.f, 100.f);
    const float b = shinyGpuHeading(200.f, 100.f, 100.f, 100.f);
    CHECK(std::fabs(a - b) < 1e-5f);
    CHECK(std::fabs(a) < 1e-5f);
}

static void checkQuantizeLatch() {
    // Heading of +x. A 1px vertical move at 100px is well under a 5° step.
    const float h0 = shinyGpuHeading(100.f, 0.f, 0.f, 0.f);
    const float q0 = shinyQuantizeHeading(h0, 0, 5);
    const float hSmall = shinyGpuHeading(100.f, -1.f, 0.f, 0.f);
    const float qSmall = shinyQuantizeHeading(hSmall, 0, 5);
    CHECK(!shinyShouldDamageHeading(q0, qSmall));

    // ~11° is past one 5° step — latch updates.
    const float hStep = shinyGpuHeading(100.f, -20.f, 0.f, 0.f);
    const float qStep = shinyQuantizeHeading(hStep, 0, 5);
    CHECK(shinyShouldDamageHeading(q0, qStep));

    // angle_offset participates in the CPU latch only, not the GPU heading.
    const float qOff = shinyQuantizeHeading(h0, 90, 1);
    const float qNo  = shinyQuantizeHeading(h0, 0, 1);
    CHECK(shinyShouldDamageHeading(qNo, qOff));
}

static void checkProductionWiring() {
    const std::string src     = sourceDir();
    const std::string deco    = readFile(src + "/deco.cpp");
    const std::string pass    = readFile(src + "/pass.cpp");
    const std::string passHpp = readFile(src + "/pass.hpp");
    const std::string runtime = readFile(src + "/runtime.hpp");
    const std::string main    = readFile(src + "/main.cpp");
    const std::string shaders = readFile(src + "/shaders.hpp");
    const std::string nest    = readFile(repoRoot() + "/nest/hyprland.lua");

    CHECK(!deco.empty());
    CHECK(!pass.empty());
    CHECK(!passHpp.empty());
    CHECK(!runtime.empty());
    CHECK(!main.empty());
    CHECK(!shaders.empty());
    CHECK(!nest.empty());

    const auto decoDraw = functionBody(deco, "void CShinyBorder::draw");
    const auto passDraw = functionBody(pass, "CShinyPassElement::draw");
    const auto mouse    = functionBody(main, "void onMouseMove()");
    CHECK(!decoDraw.empty());
    CHECK(!passDraw.empty());
    CHECK(!mouse.empty());

    // One shared set after early-outs; both backends filled from shinyMapDrawBackends.
    CHECK(decoDraw.find("ShinyDrawShared shared") != std::string::npos);
    CHECK(decoDraw.find("shinyMapDrawBackends(shared") != std::string::npos);
    CHECK(countNeedle(decoDraw, "g_cfg.colA->value()") == 1);
    CHECK(countNeedle(decoDraw, "g_cfg.colB->value()") == 1);
    CHECK(decoDraw.find("CHyprColor{sc<uint64_t>(g_cfg.colA->value())}") == std::string::npos);
    CHECK(decoDraw.find("CHyprColor{sc<uint64_t>(g_cfg.colB->value())}") == std::string::npos);

    const auto colAAt   = decoDraw.find("g_cfg.colA->value()");
    const auto colBAt   = decoDraw.find("g_cfg.colB->value()");
    const auto mapAt    = decoDraw.find("shinyMapDrawBackends");
    const auto ensureAt = decoDraw.find("ensureShinyShader()");
    const auto shinyEl  = decoDraw.find("makeUnique<CShinyPassElement>");
    const auto borderEl = decoDraw.find("makeUnique<CBorderPassElement>");
    CHECK(colAAt != std::string::npos);
    CHECK(colBAt != std::string::npos);
    CHECK(mapAt != std::string::npos);
    CHECK(ensureAt != std::string::npos);
    CHECK(shinyEl != std::string::npos);
    CHECK(borderEl != std::string::npos);
    CHECK(colAAt < mapAt);
    CHECK(colBAt < mapAt);
    CHECK(mapAt < ensureAt);
    CHECK(ensureAt < shinyEl);
    CHECK(ensureAt < borderEl);
    CHECK(shinyEl < borderEl);

    // Logical thickness into the shared set once; backends consume the mapping,
    // not a second BORDERSIZE / already-scaled BS_PX store.
    CHECK(countNeedle(decoDraw, ".borderSize    = BORDERSIZE") == 1);
    CHECK(decoDraw.find("data.borderSize    = BORDERSIZE") == std::string::npos);
    CHECK(decoDraw.find("data.borderSize    = BS_PX") == std::string::npos);
    CHECK(decoDraw.find("data.shared  = mapped.shader") != std::string::npos);
    CHECK(decoDraw.find("mapped.fallback.shared.borderSize") != std::string::npos);
    CHECK(decoDraw.find("mapped.fallback.expandPx") != std::string::npos);

    const auto fallbackAt = decoDraw.find("CBorderPassElement::SBorderData");
    CHECK(fallbackAt != std::string::npos);
    CHECK(decoDraw.find("mapped.fallback.shared.borderSize", fallbackAt) != std::string::npos);
    CHECK(decoDraw.find("data.window        = m_window", fallbackAt) != std::string::npos);

    // Shader upload multiplies logical size by mon->m_scale at most once, via the helper.
    CHECK(passDraw.find("setUniformFloat(SHADER_THICK, shinyShaderThick") != std::string::npos);
    CHECK(passDraw.find("m_data.shared.borderSize") != std::string::npos);
    CHECK(passDraw.find("m_data.borderSize) * sc<float>(mon->m_scale)") == std::string::npos);
    CHECK(passDraw.find("shared.borderSize) * sc<float>(mon->m_scale)") == std::string::npos);
    CHECK(passDraw.find("SHADER_ANGLE") == std::string::npos);

    // SData consumes ShinyDrawShared (logical px); no window handle, no parallel copy.
    const auto sdata = functionBody(passHpp, "struct SData");
    CHECK(!sdata.empty());
    CHECK(sdata.find("ShinyDrawShared") != std::string::npos);
    CHECK(sdata.find("PHLWINDOW") == std::string::npos);
    CHECK(sdata.find("PHLWINDOWREF") == std::string::npos);
    CHECK(sdata.find("window") == std::string::npos);
    CHECK(sdata.find("CHyprColor") == std::string::npos);
    CHECK(passHpp.find("PHLWINDOW") == std::string::npos);
    CHECK(passHpp.find("PHLWINDOWREF") == std::string::npos);
    CHECK(passHpp.find("logical (unscaled) px") != std::string::npos);
    CHECK(passHpp.find("scale once") != std::string::npos);
    CHECK(passHpp.find("float      angle") == std::string::npos);
    CHECK(runtime.find("logical (unscaled) px") != std::string::npos);

    // GPU heading is the visible source; CPU uses that heading then quantize.
    CHECK(mouse.find("shinyGpuHeading") != std::string::npos);
    CHECK(mouse.find("shinyQuantizeHeading") != std::string::npos);
    CHECK(mouse.find("shinyShouldDamageHeading") != std::string::npos);
    CHECK(mouse.find("std::atan2") == std::string::npos);
    const auto headingAt = mouse.find("shinyGpuHeading");
    const auto quantAt   = mouse.find("shinyQuantizeHeading");
    const auto latchAt   = mouse.find("shinyShouldDamageHeading");
    CHECK(headingAt < quantAt);
    CHECK(quantAt < latchAt);

    // Same space as SData.pointer: monitor-local, scaled.
    CHECK(mouse.find("m_floatingOffset") != std::string::npos);
    CHECK(mouse.find("m_position") != std::string::npos);
    CHECK(mouse.find("m_scale") != std::string::npos);
    const auto decoPtr = decoDraw.find("data.pointer");
    CHECK(decoPtr != std::string::npos);
    CHECK(decoDraw.find("m_floatingOffset", decoPtr) != std::string::npos);
    CHECK(decoDraw.find("m_position", decoPtr) != std::string::npos);
    CHECK(decoDraw.find("m_scale", decoPtr) != std::string::npos);

    // Fragment: always atan(-dir.y, dir.x); origin special case gone; angle uniform dropped.
    CHECK(shaders.find("atan(-dir.y, dir.x)") != std::string::npos);
    CHECK(shaders.find("dot(pointer_position, pointer_position)") == std::string::npos);
    CHECK(shaders.find("heading = angle") == std::string::npos);
    CHECK(shaders.find("uniform float angle") == std::string::npos);

    // Nest Lua key Hyprland will apply; C++ option names stay hyphenated.
    CHECK(nest.find("shiny_border") != std::string::npos);
    CHECK(nest.find("[\"shiny-border\"]") == std::string::npos);
    CHECK(main.find("plugin:shiny-border:border_size") != std::string::npos);
    CHECK(main.find("plugin:shiny-border:angle_offset") != std::string::npos);
    CHECK(main.find("plugin:shiny-border:quantize_deg") != std::string::npos);
}

int main() {
    checkDrawAgreement();
    checkThickness();
    checkHeading();
    checkQuantizeLatch();
    checkProductionWiring();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

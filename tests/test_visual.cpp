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
    // Contract: logical × monitor scale × renderModif combinedScale.
    // 3px @ 1× is 3 framebuffer px; @ 2× is 6, not 12. Zoom 2× then 12.
    CHECK(shinyShaderThick(3.f, 1.f) == 3.f);
    CHECK(shinyShaderThick(3.f, 2.f) == 6.f);
    CHECK(shinyShaderThick(3.f, 2.f) != 12.f);
    CHECK(shinyShaderThick(0.f, 2.f) == 0.f);
    CHECK(shinyShaderThick(3.f, 2.f, 1.f) == 6.f);
    CHECK(shinyShaderThick(3.f, 2.f, 2.f) == 12.f);
    // Rounding is already monitor-scaled in deco; upload multiplies combinedScale only.
    CHECK(shinyShaderThick(6.f, 1.f, 2.f) == 12.f);
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

    // Pointer is screen-relative (no floatingOffset); center is visual
    // (middle + floatingOffset). The old canceling pair — both get the
    // offset — is a different heading. Offset no longer cancels; intended.
    const float cursorX = 200.f, cursorY = 100.f;
    const float middleX = 100.f, middleY = 100.f;
    const float offX = 0.f, offY = 40.f;
    const float canceled = shinyGpuHeading(cursorX + offX, cursorY + offY, middleX + offX, middleY + offY);
    const float visual   = shinyGpuHeading(cursorX, cursorY, middleX + offX, middleY + offY);
    CHECK(std::fabs(canceled - visual) > 1e-4f);
    CHECK(std::fabs(canceled - shinyGpuHeading(cursorX, cursorY, middleX, middleY)) < 1e-5f);
}

static void checkQuantizeLatch() {
    const float pi = std::acos(-1.f);
    auto        deg = [pi](float d) { return d * pi / 180.f; };

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

    // angle_offset is part of the visible heading (shader and fallback).
    const float qOff = shinyQuantizeHeading(h0, 90, 1);
    const float qNo  = shinyQuantizeHeading(h0, 0, 1);
    CHECK(shinyShouldDamageHeading(qNo, qOff));
    CHECK(qOff >= 0.f);
    CHECK(qOff < 2.f * pi);
    CHECK(std::fabs(qOff - pi * 0.5f) < 1e-4f);

    // 350° + 90 offset = 440 without wrap; wrap lands in [0, 2π) at 80°.
    const float qWrap = shinyQuantizeHeading(deg(350.f), 90, 1);
    CHECK(qWrap >= 0.f);
    CHECK(qWrap < 2.f * pi);
    CHECK(std::fabs(qWrap - deg(80.f)) < 1e-3f);

    // 359° vs 0° with step 5: floor buckets are 355 and 0 (5° apart) → damage.
    // Offset 1 puts both in the 0 bucket after wrap → no damage.
    CHECK(shinyShouldDamageHeading(shinyQuantizeHeading(deg(359.f), 0, 5),
                                   shinyQuantizeHeading(deg(0.f), 0, 5)));
    CHECK(shinyShouldDamageHeading(shinyQuantizeHeading(deg(355.f), 0, 5),
                                   shinyQuantizeHeading(deg(0.f), 0, 5)));
    CHECK(!shinyShouldDamageHeading(shinyQuantizeHeading(deg(359.f), 1, 5),
                                    shinyQuantizeHeading(deg(0.f), 1, 5)));
    CHECK(!shinyShouldDamageHeading(shinyQuantizeHeading(deg(356.f), 0, 5),
                                    shinyQuantizeHeading(deg(359.f), 0, 5)));

    // Raw fabs at ~2π vs 0 looks like a full turn; shortest arc can be a no-op.
    CHECK(!shinyShouldDamageHeading(2.f * pi - 1e-5f, 0.f));
    CHECK(shinyShouldDamageHeading(deg(359.f), deg(0.f)));
}

static std::string sourceDir() {
    std::string       file     = __FILE__;
    const auto        slash    = file.find_last_of('/');
    const std::string testsDir = (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
    return testsDir + "/../src";
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void checkShaderSource() {
    const std::string frag = readFile(sourceDir() + "/shaders.hpp");
    CHECK(!frag.empty());
    CHECK(frag.find("heading = angle") != std::string::npos);
    CHECK(frag.find("max(roundingPower, 2.0)") == std::string::npos);
    CHECK(frag.find("brightness <= 0.0") != std::string::npos);
    CHECK(frag.find("pointer_position") == std::string::npos);
    CHECK(frag.find("atan(-dir.y") == std::string::npos);
}

static void checkProductionWiring() {
    const std::string deco = readFile(sourceDir() + "/deco.cpp");
    const std::string pass = readFile(sourceDir() + "/pass.cpp");
    const std::string plug = readFile(sourceDir() + "/main.cpp");
    CHECK(!deco.empty());
    CHECK(!pass.empty());
    CHECK(!plug.empty());

    CHECK(deco.find("data.angle") != std::string::npos);
    CHECK(deco.find("m_angle") != std::string::npos);
    CHECK(deco.find("getMouseCoordsInternal") == std::string::npos);
    CHECK(deco.find("data.pointer") == std::string::npos);

    CHECK(pass.find("SHADER_ANGLE") != std::string::npos);
    CHECK(pass.find("m_data.angle") != std::string::npos);
    CHECK(pass.find("SHADER_POINTER") == std::string::npos);
    CHECK(pass.find("m_data.pointer") == std::string::npos);

    // Shimmer is CPU-modulated: the deco steps the walk and hands the pass a
    // final angle / lobe / thickness scale; the shader stays pulse-or-nominal.
    CHECK(deco.find("shinyShimmerStep") != std::string::npos);
    CHECK(deco.find("shinyShimmerLobe") != std::string::npos);
    CHECK(deco.find("SHINY_EFFECT_PULSE") != std::string::npos);
    CHECK(pass.find("m_data.thickScale") != std::string::npos);

    // Pin replaces the mouse latch: the deco computes the pinned heading and
    // the mouse-move listener bails out before touching any latch.
    CHECK(deco.find("shinyPinnedHeading") != std::string::npos);
    CHECK(plug.find("g_cfg.pin->value()") != std::string::npos);
}

int main() {
    checkDrawAgreement();
    checkThickness();
    checkHeading();
    checkQuantizeLatch();
    checkShaderSource();
    checkProductionWiring();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

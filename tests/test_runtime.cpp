#include "../src/runtime.hpp"

#include <cstdio>

static int g_fails = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

static void checkShippedDecisions() {
    // Unmapped → no-op (do not touch positioner / renderer).
    CHECK(!shinyCanUseMappedGeometry(false, true));
    CHECK(!shinyCanDamage(false, true, false));

    // Missing renderer → no-op.
    CHECK(!shinyCanUseMappedGeometry(true, false));
    CHECK(!shinyCanDamage(true, false, false));

    // Both missing → no-op, including when exclusive-fullscreen is claimed.
    CHECK(!shinyCanUseMappedGeometry(false, false));
    CHECK(!shinyCanDamage(false, false, true));

    // Mapped + renderer, not exclusive fullscreen → proceed.
    CHECK(shinyCanUseMappedGeometry(true, true));
    CHECK(shinyCanDamage(true, true, false));

    // Exclusive fullscreen → no-op even when mapped + renderer are live.
    CHECK(!shinyCanDamage(true, true, true));

    // Mapped/renderer fail even if exclusiveFullscreen is false: the fullscreen
    // flag must not paper over the first gate.
    CHECK(!shinyCanDamage(false, true, false));
    CHECK(!shinyCanDamage(true, false, false));

    // VAO -1 (createVao failure) and 0 (default object) → do not bind.
    CHECK(!shinyCanBindVao(-1));
    CHECK(!shinyCanBindVao(0));

    // Real VAO names are positive → bind is allowed.
    CHECK(shinyCanBindVao(1));
    CHECK(shinyCanBindVao(42));

    // Thickness: logical × monitor × combinedScale. Default modif scale is 1.
    CHECK(shinyShaderThick(3.f, 2.f) == 6.f);
    CHECK(shinyShaderThick(3.f, 2.f, 1.f) == 6.f);
    CHECK(shinyShaderThick(3.f, 2.f, 2.f) == 12.f);
}

static void checkPulseDecisions() {
    // pulse false → should not run, regardless of focus / active_only / hz.
    CHECK(!shinyPulseShouldRun(true, false, 0.4f, true, true));
    CHECK(!shinyPulseShouldRun(true, false, 0.4f, true, false));
    CHECK(!shinyPulseShouldRun(true, false, 0.4f, false, true));
    CHECK(!shinyPulseShouldRun(true, false, 0.4f, false, false));

    // pulse true + active_only true + not focused → should not run; focused → should run.
    CHECK(!shinyPulseShouldRun(true, true, 0.4f, true, false));
    CHECK(shinyPulseShouldRun(true, true, 0.4f, true, true));

    // pulse true + active_only false → should run for focused and unfocused.
    CHECK(shinyPulseShouldRun(true, true, 0.4f, false, true));
    CHECK(shinyPulseShouldRun(true, true, 0.4f, false, false));

    // plugin enabled false → should not run.
    CHECK(!shinyPulseShouldRun(false, true, 0.4f, true, true));
    CHECK(!shinyPulseShouldRun(false, true, 0.4f, false, false));
    CHECK(!shinyPulseShouldRun(false, false, 0.4f, false, true));

    // pulse true + hz <= 0 → should not run (even when focused).
    CHECK(!shinyPulseShouldRun(true, true, 0.f, true, true));
    CHECK(!shinyPulseShouldRun(true, true, -0.1f, false, true));

    // pulse false → uniforms time = 0 and pulseHz = 0 even when clock and Hz are non-zero.
    const auto off = shinyPulseUniforms(false, 12.5, 0.4f);
    CHECK(off.time == 0.f);
    CHECK(off.pulseHz == 0.f);
    const auto offOther = shinyPulseUniforms(false, 3.0, 4.f);
    CHECK(offOther.time == 0.f);
    CHECK(offOther.pulseHz == 0.f);

    // pulse true + hz 0 → zeros.
    const auto offHz = shinyPulseUniforms(true, 12.5, 0.f);
    CHECK(offHz.time == 0.f);
    CHECK(offHz.pulseHz == 0.f);

    // pulse true + hz 0.4 → time wrapped into one 1/hz period [0, 2.5).
    const auto on = shinyPulseUniforms(true, 12.5, 0.4f);
    CHECK(on.time >= 0.f);
    CHECK(on.time < 2.5f);
    CHECK(on.pulseHz == 0.4f);
    const auto onMid = shinyPulseUniforms(true, 13.0, 0.4f);
    CHECK(onMid.time > 0.49f);
    CHECK(onMid.time < 0.51f);
    CHECK(onMid.pulseHz == 0.4f);
    const auto onFast = shinyPulseUniforms(true, 0.25, 4.f);
    CHECK(onFast.time >= 0.f);
    CHECK(onFast.time < 0.25f);
    CHECK(onFast.pulseHz == 4.f);

    // Re-arm period is not a once-per-cycle 1/Hz fire (that hitchs vs compositor time).
    CHECK(shinyPulseTickMs(0.4f) > 0);
    CHECK(shinyPulseTickMs(0.4f) < static_cast<int>(1000.f / 0.4f));
    CHECK(shinyPulseTickMs(4.f) > 0);
    CHECK(shinyPulseTickMs(4.f) < static_cast<int>(1000.f / 4.f));
}

static void checkEffectiveBorderSize() {
    CHECK(shinyEffectiveBorderSize(3, false, true, true) == 0);
    CHECK(shinyEffectiveBorderSize(3, true, true, false) == 0);
    CHECK(shinyEffectiveBorderSize(3, true, true, true) == 3);
    CHECK(shinyEffectiveBorderSize(3, true, false, false) == 3);
    CHECK(shinyEffectiveBorderSize(0, true, true, true) == 0);
}

static void checkUpdateWindowActions() {
    const ShinyGeoLatch geo{10.0, 20.0, 100.0, 200.0};
    const int           bs = 3;

    // No geo change + no effective-border change → no reposition, no damage.
    auto a = shinyUpdateWindowActions(geo, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(!a.damage);

    // Geo change + same effective border → no reposition, damage.
    const ShinyGeoLatch moved{11.0, 20.0, 100.0, 200.0};
    a = shinyUpdateWindowActions(moved, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(a.damage);

    const ShinyGeoLatch resized{10.0, 20.0, 100.0, 201.0};
    a = shinyUpdateWindowActions(resized, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(a.damage);

    // Same geo + effective border change → reposition and damage.
    a = shinyUpdateWindowActions(geo, 4, geo, bs);
    CHECK(a.reposition);
    CHECK(a.damage);

    // Same geo, effective 3 → 0 (enabled off / active_only unfocused).
    a = shinyUpdateWindowActions(geo, 0, geo, 3);
    CHECK(a.reposition);
    CHECK(a.damage);

    // Same geo, 3 → 3 → neither.
    a = shinyUpdateWindowActions(geo, 3, geo, 3);
    CHECK(!a.reposition);
    CHECK(!a.damage);

    // Both change → reposition and damage.
    a = shinyUpdateWindowActions(moved, 4, geo, bs);
    CHECK(a.reposition);
    CHECK(a.damage);
}

int main() {
    checkShippedDecisions();
    checkPulseDecisions();
    checkEffectiveBorderSize();
    checkUpdateWindowActions();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

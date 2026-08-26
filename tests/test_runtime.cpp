#include "../src/runtime.hpp"

#include <cmath>
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

static void checkEffectExclusivity() {
    // Shimmer wins when both are configured on.
    CHECK(shinyEffectMode(true, 0.4f, true, 0.6f) == SHINY_EFFECT_SHIMMER);
    CHECK(shinyEffectMode(false, 0.4f, true, 0.6f) == SHINY_EFFECT_SHIMMER);

    // Shimmer off or hz <= 0 falls through to pulse.
    CHECK(shinyEffectMode(true, 0.4f, false, 0.6f) == SHINY_EFFECT_PULSE);
    CHECK(shinyEffectMode(true, 0.4f, true, 0.f) == SHINY_EFFECT_PULSE);
    CHECK(shinyEffectMode(true, 0.4f, true, -1.f) == SHINY_EFFECT_PULSE);

    // Pulse hz <= 0 disables pulse too.
    CHECK(shinyEffectMode(true, 0.f, false, 0.6f) == SHINY_EFFECT_NONE);
    CHECK(shinyEffectMode(false, 0.4f, false, 0.6f) == SHINY_EFFECT_NONE);
    CHECK(shinyEffectMode(true, 0.f, true, 0.f) == SHINY_EFFECT_NONE);

    // Timer gate: same shape as the pulse gate, but mode-driven.
    CHECK(!shinyEffectShouldRun(true, SHINY_EFFECT_NONE, true, true));
    CHECK(!shinyEffectShouldRun(false, SHINY_EFFECT_SHIMMER, true, true));
    CHECK(!shinyEffectShouldRun(true, SHINY_EFFECT_SHIMMER, true, false));
    CHECK(shinyEffectShouldRun(true, SHINY_EFFECT_SHIMMER, true, true));
    CHECK(shinyEffectShouldRun(true, SHINY_EFFECT_SHIMMER, false, false));
    CHECK(shinyEffectShouldRun(true, SHINY_EFFECT_PULSE, true, true));

    // Tick period follows the active effect's hz.
    CHECK(shinyEffectTickMs(SHINY_EFFECT_PULSE, 0.4f, 4.f) == shinyPulseTickMs(0.4f));
    CHECK(shinyEffectTickMs(SHINY_EFFECT_SHIMMER, 0.4f, 4.f) == shinyPulseTickMs(4.f));
    CHECK(shinyEffectTickMs(SHINY_EFFECT_SHIMMER, 0.4f, 0.6f) > 0);
    CHECK(shinyEffectTickMs(SHINY_EFFECT_SHIMMER, 0.4f, 0.6f) < static_cast<int>(1000.f / 0.6f));
}

static void checkPinnedHeading() {
    const float pi = std::acos(-1.f);

    CHECK(std::fabs(shinyPinnedHeading(0, 0)) < 1e-5f);
    CHECK(std::fabs(shinyPinnedHeading(90, 0) - pi * 0.5f) < 1e-5f);
    CHECK(std::fabs(shinyPinnedHeading(180, 0) - pi) < 1e-5f);

    // angle_offset still applies, and the sum wraps into [0, 2π).
    CHECK(std::fabs(shinyPinnedHeading(350, 20) - 10.f * pi / 180.f) < 1e-4f);
    CHECK(std::fabs(shinyPinnedHeading(-90, 0) - 270.f * pi / 180.f) < 1e-4f);

    const float p = shinyPinnedHeading(-360, -180);
    CHECK(p >= 0.f);
    CHECK(p < 2.f * pi);

    // Wrap helper on its own.
    CHECK(std::fabs(shinyWrapAngle(2.f * pi + 0.1f) - 0.1f) < 1e-4f);
    CHECK(std::fabs(shinyWrapAngle(-0.1f) - (2.f * pi - 0.1f)) < 1e-4f);
    CHECK(shinyWrapAngle(0.f) == 0.f);
}

static void checkShimmer() {
    const ShinyShimmerParams p{.hz = 0.6f, .angleRangeRad = 0.4363f, .scaleMin = 0.75f, .scaleMax = 1.35f};

    // hz <= 0 or dt <= 0 → no-op.
    ShinyShimmerState idle;
    shinyShimmerSeed(idle, 7);
    shinyShimmerStep(idle, 0.f, p);
    CHECK(idle.angle.value == 0.f);
    CHECK(idle.scale.value == 1.f);
    shinyShimmerStep(idle, 0.016f, ShinyShimmerParams{.hz = 0.f});
    CHECK(idle.angle.value == 0.f);
    CHECK(idle.scale.value == 1.f);

    // Determinism: same seed, same steps → same values.
    ShinyShimmerState a, b;
    shinyShimmerSeed(a, 42);
    shinyShimmerSeed(b, 42);
    for (int i = 0; i < 500; i++) {
        shinyShimmerStep(a, 0.016f, p);
        shinyShimmerStep(b, 0.016f, p);
    }
    CHECK(a.angle.value == b.angle.value);
    CHECK(a.scale.value == b.scale.value);

    // Seed 0 must not wedge xorshift at 0 (state would never move).
    ShinyShimmerState z;
    shinyShimmerSeed(z, 0);
    for (int i = 0; i < 500; i++)
        shinyShimmerStep(z, 0.016f, p);
    CHECK(z.scale.value != 1.f || z.angle.value != 0.f);

    // Bounds + actual movement over a long run.
    ShinyShimmerState s;
    shinyShimmerSeed(s, 1234);
    float minAngle = 1e9f, maxAngle = -1e9f, minScale = 1e9f, maxScale = -1e9f;
    for (int i = 0; i < 4000; i++) { // ~64 s at 16 ms
        shinyShimmerStep(s, 0.016f, p);
        minAngle = std::fmin(minAngle, s.angle.value);
        maxAngle = std::fmax(maxAngle, s.angle.value);
        minScale = std::fmin(minScale, s.scale.value);
        maxScale = std::fmax(maxScale, s.scale.value);
        CHECK(std::fabs(s.angle.value) <= p.angleRangeRad + 1e-5f);
        CHECK(s.scale.value >= p.scaleMin - 1e-5f);
        CHECK(s.scale.value <= p.scaleMax + 1e-5f);
    }
    // The walk visits both sides of the heading and actually resizes.
    CHECK(minAngle < -0.01f);
    CHECK(maxAngle > 0.01f);
    CHECK(maxScale - minScale > 0.1f);

    // Independence: the two channels draw separate durations, so their
    // retarget clocks are not in lockstep.
    CHECK(s.angle.dur != s.scale.dur);
    CHECK(s.angle.t != s.scale.t || s.angle.dur != s.scale.dur);

    // Inverted scale range is swapped, not a hole.
    ShinyShimmerState inv;
    shinyShimmerSeed(inv, 99);
    const ShinyShimmerParams pInv{.hz = 1.f, .angleRangeRad = 0.1f, .scaleMin = 1.4f, .scaleMax = 0.8f};
    for (int i = 0; i < 2000; i++) {
        shinyShimmerStep(inv, 0.016f, pInv);
        CHECK(inv.scale.value >= 0.8f - 1e-5f);
        CHECK(inv.scale.value <= 1.4f + 1e-5f);
    }

    // Zero angle range: the offset eases to 0 and stays there.
    ShinyShimmerState flat;
    shinyShimmerSeed(flat, 5);
    const ShinyShimmerParams pFlat{.hz = 1.f, .angleRangeRad = 0.f, .scaleMin = 0.9f, .scaleMax = 1.1f};
    for (int i = 0; i < 2000; i++)
        shinyShimmerStep(flat, 0.016f, pFlat);
    CHECK(std::fabs(flat.angle.value) < 1e-5f);

    // Effective lobe clamps to the config range; thickness stays muted.
    CHECK(shinyShimmerLobe(0.18f, 1.f) == 0.18f);
    CHECK(shinyShimmerLobe(0.18f, 10.f) == 0.5f);
    CHECK(shinyShimmerLobe(0.18f, 0.01f) == 0.04f);
    CHECK(shinyShimmerThickScale(1.f) == 1.f);
    CHECK(shinyShimmerThickScale(2.f) < 2.f);
    CHECK(shinyShimmerThickScale(2.f) > 1.f);
    CHECK(shinyShimmerThickScale(0.f) > 0.f);
}

static void checkEffectiveBorderSize() {
    CHECK(shinyEffectiveBorderSize(3, false) == 0);
    CHECK(shinyEffectiveBorderSize(3, true) == 3);
    CHECK(shinyEffectiveBorderSize(0, true) == 0);
    CHECK(shinyEffectiveBorderSize(0, false) == 0);
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

    // Same geo, effective 3 → 0 (enabled off).
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
    checkEffectExclusivity();
    checkPinnedHeading();
    checkShimmer();
    checkEffectiveBorderSize();
    checkUpdateWindowActions();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

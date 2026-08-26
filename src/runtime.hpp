#pragma once

#include <cstdint>

// Runtime decisions used by assignedBoxGlobal, damageEntire, shader draw,
// border-size fallback, and the unified draw-backend mapping. Hyprland-free
// so tests can drive the same functions without a compositor.

// assignedBoxGlobal / first gate of damageEntire:
// do not call getEdgeDefinedPoint or g_pHyprRenderer->damageRegion without a
// mapped window and a live renderer.
bool shinyCanUseMappedGeometry(bool mapped, bool rendererAlive);

// damageEntire after the mapped/renderer gate is known true:
// skip exclusive fullscreen (FSMODE_FULLSCREEN) — renderWindow already sets
// decorate = false for that mode. Mapped/renderer are still required so a
// caller cannot skip those by passing exclusiveFullscreen = false.
bool shinyCanDamage(bool mapped, bool rendererAlive, bool exclusiveFullscreen);

// shader draw: do not glBindVertexArray when the stashed VAO id is <= 0.
// createVao() can leave SHADER_SHADER_VAO at -1; binding 0xFFFFFFFF is
// GL_INVALID_OPERATION and a compositor kill on NVIDIA.
bool shinyCanBindVao(int vao);

// plugin:shiny-border:border_size vs general:border_size.
// configured >= 0 wins (including 0 = no ring); -1 follows general.
int shinyResolvedBorderSize(int configured, int generalBorderSize);

// Reserved extent after enabled. 0 if the plugin is off, otherwise
// resolvedPx. Unfocused windows still reserve (active_only only skips
// the shader / pulse / heading). Positioning uses this; drawing still
// uses the resolved px.
int shinyEffectiveBorderSize(int resolvedPx, bool enabled);

// Shader-path ring thickness: logical × monitor scale × renderModif combinedScale.
// SData.borderSize stores logical (unscaled) px; pass that in. Pre-scaling
// it and multiplying again is the double-scale bug (3px @ 2× → 12).
// combinedScale() is applied at upload in pass.cpp so deco does not need a
// modif it cannot see. Default 1 is identity (no zoom / no workspace scale).
float shinyShaderThick(float logicalPx, float monitorScale, float modifScale = 1.f);

// Heading from pointer vs box center, GLSL atan(-dir.y, dir.x) convention.
// Live cursor feeds the mouse-move latch only — not the fragment.
float shinyGpuHeading(float pointerX, float pointerY, float centerX, float centerY);

// Visible heading: snap to degStep, add offset, wrap into [0, 2π).
// Shader (SHADER_ANGLE) and fallback (m_angle) both draw this value.
float shinyQuantizeHeading(float radians, int offsetDeg, int degStep);

// True when the quantized heading moved enough to damage the ring.
// Compares on the circle so 359° vs 0° is one step, not a full turn.
bool shinyShouldDamageHeading(float latched, float next);

// Dirty-check used by CShinyBorder::updateWindow. Hyprland-free so tests
// can drive the same decision the deco calls.
// Reposition only when effective border size changed (m_lastEffectiveB).
// Damage when window pos/size or that effective size changed.
// Unchanged geometry + unchanged effective border → neither
// (unrelated config reload).
struct ShinyGeoLatch {
    double posX  = 0;
    double posY  = 0;
    double sizeX = 0;
    double sizeY = 0;
};

struct ShinyUpdateActions {
    bool reposition = false;
    bool damage     = false;
};

ShinyUpdateActions shinyUpdateWindowActions(const ShinyGeoLatch& now, int effectiveBorder, const ShinyGeoLatch& last,
                                            int lastEffectiveBorder);

// Shared draw fields both backends consume. Colors are packed Hyprland
// CHyprColor uint64 (same as sc<uint64_t>(g_cfg.colA->value())).
// borderSize is logical (unscaled) px — SHADER_THICK / CBorderPassElement
// GL scale once. Do not store already-scaled px here.
struct ShinyDrawShared {
    int      rounding      = 0;
    int      outerRound    = 0;
    float    roundingPower = 2.f;
    float    a             = 1.f;
    int      borderSize    = 3; // logical (unscaled) px. Scale once (phase 4).
    uint64_t colA          = 0;
    uint64_t colB          = 0;
};

// Fallback-only: inner-box expand/inset px = round(logical × monitor scale).
struct ShinyFallbackPass {
    ShinyDrawShared shared;
    int             expandPx = 0;
};

struct ShinyDrawBackends {
    ShinyDrawShared   shader;
    ShinyFallbackPass fallback;
};

// Map one shared set into both backend payloads. Shader keeps logical
// borderSize; fallback expandPx is the only scale applied here.
ShinyDrawBackends shinyMapDrawBackends(const ShinyDrawShared& p, float monitorScale);

int shinyFallbackExpandPx(int logicalPx, float monitorScale);

// Per-deco pulse scheduler: should this deco keep a running pulse
// (timer that damageEntire's)? false if !enabled || !pulse || pulseHz <= 0.
// active_only → only the focused deco; otherwise every mapped shiny deco.
bool shinyPulseShouldRun(bool enabled, bool pulse, float pulseHz, bool activeOnly, bool focused);

// Shader time / pulseHz. Pulse off or hz <= 0 → both zero even if the
// clock is non-zero. Otherwise wrap clockSeconds (double) to one 1/hz
// period, then narrow. One clock: compositor m_globalTimer — not a mix.
struct ShinyPulseUniforms {
    float time    = 0.f;
    float pulseHz = 0.f;
};

ShinyPulseUniforms shinyPulseUniforms(bool pulse, double clockSeconds, float hz);

// Re-arm period for per-deco pulse damage, milliseconds. Must be much
// smaller than one sine cycle (1/pulseHz) so compositor-time sampling
// does not hitch. Not a process-wide tick.
int shinyPulseTickMs(float pulseHz);

// Which oscillation drives the ring. Shimmer and pulse are mutually
// exclusive: shimmer wins when both are configured on. An effect whose
// hz is <= 0 is off and falls through (shimmer_hz 0 + pulse on → pulse).
enum ShinyEffect {
    SHINY_EFFECT_NONE = 0,
    SHINY_EFFECT_PULSE,
    SHINY_EFFECT_SHIMMER,
};

ShinyEffect shinyEffectMode(bool pulse, float pulseHz, bool shimmer, float shimmerHz);

// Timer gate for whichever effect is active. Same shape as
// shinyPulseShouldRun but mode-driven: false when the plugin is off or
// no effect is on; active_only → only the focused deco keeps a timer.
bool shinyEffectShouldRun(bool enabled, ShinyEffect mode, bool activeOnly, bool focused);

// Re-arm period for the active effect. Pulse samples its sine; shimmer
// samples its eased random walk. Same clamp as shinyPulseTickMs.
int shinyEffectTickMs(ShinyEffect mode, float pulseHz, float shimmerHz);

// Wrap radians into [0, 2π). Shimmer adds a signed offset to the latched
// heading; the shader and the fallback gradient both expect a wrapped angle.
float shinyWrapAngle(float radians);

// Pinned heading: (pinDeg + offsetDeg) degrees → radians in [0, 2π).
// GLSL atan(-y, x) convention: 0° faces +x (right), 90° faces up.
// Replaces the mouse latch when pin is on; angle_offset still applies.
float shinyPinnedHeading(int pinDeg, int offsetDeg);

// Shimmer: two independent random-walk channels. Each channel eases
// (smoothstep) from its current value to a random target, then draws a
// new target and a new duration — angle and scale retarget on their own
// clocks, so heading drift and resize are visibly decoupled.
struct ShinyShimmerChannel {
    float value = 0.f;
    float from  = 0.f;
    float to    = 0.f;
    float t     = 0.f; // elapsed seconds within the current ease
    float dur   = 0.f; // <= 0 means "pick a target on the next step"
};

struct ShinyShimmerState {
    ShinyShimmerChannel angle; // signed radians around the base heading
    ShinyShimmerChannel scale = {.value = 1.f, .from = 1.f, .to = 1.f};
    uint32_t            rng   = 0x9E3779B9u; // xorshift32; never 0
};

struct ShinyShimmerParams {
    float hz            = 0.6f;     // average retargets per second, per channel
    float angleRangeRad = 0.4363f;  // max |offset| from the base heading (±25°)
    float scaleMin      = 0.75f;    // lobe / thickness scale bounds; swapped if inverted
    float scaleMax      = 1.35f;
};

// Deterministic per-deco stream. seed 0 would wedge xorshift32 at 0 —
// it is replaced with a fixed non-zero constant.
void shinyShimmerSeed(ShinyShimmerState& s, uint32_t seed);

// Advance both channels by dt seconds. hz <= 0 or dt <= 0 is a no-op.
// angle.value stays within ±angleRangeRad; scale.value converges into
// [scaleMin, scaleMax] (one ease if it starts outside).
void shinyShimmerStep(ShinyShimmerState& s, float dt, const ShinyShimmerParams& p);

// Effective highlight half-width: lobe × shimmer scale, clamped to the
// same [0.04, 0.5] the config allows so a wild scale range cannot draw
// a degenerate or full-circle lobe.
float shinyShimmerLobe(float lobe, float scale);

// Ring thickness responds to the scale channel too, but muted (35%),
// mirroring how pulse breathes spread harder than thickness.
float shinyShimmerThickScale(float scale);

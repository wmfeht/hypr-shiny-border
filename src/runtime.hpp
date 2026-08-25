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

// Shader-path ring thickness: logical px × monitor scale, once.
// SData.borderSize stores logical (unscaled) px; pass that in. Pre-scaling
// it and multiplying again is the double-scale bug (3px @ 2× → 12).
float shinyShaderThick(float logicalPx, float monitorScale);

// Visible heading: GLSL atan(-dir.y, dir.x) from pointer vs box center.
// Same space as SData.pointer / shader pointer_position. Pointer at the
// transformed origin is still this atan — no CPU-angle fallback.
float shinyGpuHeading(float pointerX, float pointerY, float centerX, float centerY);

// CPU damage latch: snap heading to degStep, with angle_offset. Visual
// heading is shinyGpuHeading; this is not a second visible heading.
float shinyQuantizeHeading(float radians, int offsetDeg, int degStep);

// True when the quantized heading moved enough to damage the ring.
bool shinyShouldDamageHeading(float latched, float nextQuantized);

// Dirty-check used by CShinyBorder::updateWindow. Hyprland-free so tests
// can drive the same decision the deco calls.
// Reposition only when resolved border size changed (m_lastSizeB).
// Damage when window pos/size or that border size changed.
// Unchanged geometry + unchanged border → neither (unrelated config reload).
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

ShinyUpdateActions shinyUpdateWindowActions(const ShinyGeoLatch& now, int borderSize, const ShinyGeoLatch& last,
                                            int lastBorderSize);

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
// (timer/avar that damageEntire's)? Pulse false or plugin off → no.
// active_only → only the focused deco; otherwise every mapped shiny deco.
bool shinyPulseShouldRun(bool enabled, bool pulse, bool activeOnly, bool focused);

// Shader time / pulseHz. Pulse off → both zero even if the clock and
// configured Hz are non-zero. Pulse on → that clock and that Hz.
// One clock: compositor m_globalTimer (or the animated value) — not a mix.
struct ShinyPulseUniforms {
    float time    = 0.f;
    float pulseHz = 0.f;
};

ShinyPulseUniforms shinyPulseUniforms(bool pulse, float clockSeconds, float configuredHz);

// Re-arm period for per-deco pulse damage, milliseconds. Must be much
// smaller than one sine cycle (1/pulseHz) so compositor-time sampling
// does not hitch. Not a process-wide tick.
int shinyPulseTickMs(float pulseHz);

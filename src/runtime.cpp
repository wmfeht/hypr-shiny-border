#include "runtime.hpp"

#include <algorithm>
#include <cmath>

bool shinyCanUseMappedGeometry(bool mapped, bool rendererAlive) {
    return mapped && rendererAlive;
}

bool shinyCanDamage(bool mapped, bool rendererAlive, bool exclusiveFullscreen) {
    if (!shinyCanUseMappedGeometry(mapped, rendererAlive))
        return false;
    if (exclusiveFullscreen)
        return false;
    return true;
}

bool shinyCanBindVao(int vao) {
    return vao > 0;
}

int shinyResolvedBorderSize(int configured, int generalBorderSize) {
    if (configured >= 0)
        return configured;
    return generalBorderSize;
}

float shinyShaderThick(float logicalPx, float monitorScale) {
    return logicalPx * monitorScale;
}

float shinyGpuHeading(float pointerX, float pointerY, float centerX, float centerY) {
    return std::atan2(-(pointerY - centerY), pointerX - centerX);
}

float shinyQuantizeHeading(float radians, int offsetDeg, int degStep) {
    const int   step = std::max(1, degStep);
    const float pi   = std::acos(-1.f);
    float       deg  = std::fmod(std::fmod(radians * 180.f / pi, 360.f) + 360.f, 360.f);
    deg              = std::floor((deg + static_cast<float>(offsetDeg)) / static_cast<float>(step)) * static_cast<float>(step);
    return deg * pi / 180.f;
}

bool shinyShouldDamageHeading(float latched, float nextQuantized) {
    return std::fabs(nextQuantized - latched) >= 1e-4f;
}

ShinyUpdateActions shinyUpdateWindowActions(const ShinyGeoLatch& now, int borderSize, const ShinyGeoLatch& last,
                                            int lastBorderSize) {
    const bool borderChanged = (borderSize != lastBorderSize);
    const bool geoChanged    = (now.posX != last.posX || now.posY != last.posY || now.sizeX != last.sizeX ||
                                now.sizeY != last.sizeY);
    return ShinyUpdateActions{
        .reposition = borderChanged,
        .damage     = geoChanged || borderChanged,
    };
}

int shinyFallbackExpandPx(int logicalPx, float monitorScale) {
    return static_cast<int>(std::round(static_cast<float>(logicalPx) * monitorScale));
}

ShinyDrawBackends shinyMapDrawBackends(const ShinyDrawShared& p, float monitorScale) {
    ShinyDrawBackends out;
    out.shader            = p;
    out.fallback.shared   = p;
    out.fallback.expandPx = shinyFallbackExpandPx(p.borderSize, monitorScale);
    return out;
}

bool shinyPulseShouldRun(bool enabled, bool pulse, bool activeOnly, bool focused) {
    if (!enabled || !pulse)
        return false;
    if (activeOnly && !focused)
        return false;
    return true;
}

ShinyPulseUniforms shinyPulseUniforms(bool pulse, float clockSeconds, float configuredHz) {
    if (!pulse)
        return {.time = 0.f, .pulseHz = 0.f};
    return {.time = clockSeconds, .pulseHz = configuredHz};
}

int shinyPulseTickMs(float pulseHz) {
    // ~32 samples per sine cycle, clamped so a slow pulse still breathes
    // (not one damage per 1/Hz cycle) and a fast pulse does not exceed ~60fps.
    constexpr int kMinMs = 16;
    constexpr int kMaxMs = 50;
    if (pulseHz <= 0.f)
        return kMinMs;
    const float cycleMs = 1000.f / pulseHz;
    const int   sampled = static_cast<int>(cycleMs / 32.f);
    if (sampled < kMinMs)
        return kMinMs;
    if (sampled > kMaxMs)
        return kMaxMs;
    return sampled;
}

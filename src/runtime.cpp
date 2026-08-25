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

int shinyEffectiveBorderSize(int resolvedPx, bool enabled, bool activeOnly, bool focused) {
    if (!enabled)
        return 0;
    if (activeOnly && !focused)
        return 0;
    return resolvedPx;
}

float shinyShaderThick(float logicalPx, float monitorScale, float modifScale) {
    return logicalPx * monitorScale * modifScale;
}

float shinyGpuHeading(float pointerX, float pointerY, float centerX, float centerY) {
    return std::atan2(-(pointerY - centerY), pointerX - centerX);
}

float shinyQuantizeHeading(float radians, int offsetDeg, int degStep) {
    const int   step = std::max(1, degStep);
    const float pi   = std::acos(-1.f);
    float       deg  = std::fmod(std::fmod(radians * 180.f / pi, 360.f) + 360.f, 360.f);
    deg              = std::floor((deg + static_cast<float>(offsetDeg)) / static_cast<float>(step)) * static_cast<float>(step);
    deg              = std::fmod(std::fmod(deg, 360.f) + 360.f, 360.f);
    return deg * pi / 180.f;
}

bool shinyShouldDamageHeading(float latched, float next) {
    float       d    = std::fabs(next - latched);
    const float pi   = std::acos(-1.f);
    const float turn = 2.f * pi;
    d                = std::fmod(d, turn);
    if (d > pi)
        d = turn - d; // shortest arc
    return d >= 1e-4f;
}

ShinyUpdateActions shinyUpdateWindowActions(const ShinyGeoLatch& now, int effectiveBorder, const ShinyGeoLatch& last,
                                            int lastEffectiveBorder) {
    const bool borderChanged = (effectiveBorder != lastEffectiveBorder);
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

bool shinyPulseShouldRun(bool enabled, bool pulse, float pulseHz, bool activeOnly, bool focused) {
    if (!enabled || !pulse || pulseHz <= 0.f)
        return false;
    if (activeOnly && !focused)
        return false;
    return true;
}

ShinyPulseUniforms shinyPulseUniforms(bool pulse, double clockSeconds, float hz) {
    if (!pulse || hz <= 0.f)
        return {.time = 0.f, .pulseHz = 0.f};
    const double wrapped = std::fmod(clockSeconds, 1.0 / static_cast<double>(hz));
    return {.time = static_cast<float>(wrapped), .pulseHz = hz};
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

#pragma once

#include <optional>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

inline HANDLE PHANDLE = nullptr;

struct SShinyConfig {
    SP<Config::Values::CBoolValue>  enabled;
    SP<Config::Values::CBoolValue>  activeOnly;
    SP<Config::Values::CBoolValue>  pulse;
    SP<Config::Values::CIntValue>   quantizeDeg;
    SP<Config::Values::CIntValue>   angleOffset;
    SP<Config::Values::CIntValue>   borderSize;
    SP<Config::Values::CFloatValue> pulseHz;
    SP<Config::Values::CFloatValue> lobe;
    SP<Config::Values::CColorValue> colA;
    SP<Config::Values::CColorValue> colB;
    // Pointer into Hyprland's CConfigValueBase::registry(). Bound in
    // PLUGIN_INIT, reset in PLUGIN_EXIT — must not be a function-local static
    // (that destructor runs during dlclose; flushCaches() can UAF).
    std::optional<CConfigValue<Config::INTEGER>> generalBorderSize;
};

inline constexpr const char* kGeneralBorderSizeKey = "general:border_size";

inline SShinyConfig g_cfg;

// Keep these alive: hyprutils unregisters the listener when the SP dies.
inline CHyprSignalListener g_onWindowOpen;
inline CHyprSignalListener g_onMouseMove;
inline CHyprSignalListener g_onFocus;

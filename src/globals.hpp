#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
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
};

inline SShinyConfig g_cfg;

// Keep these alive: hyprutils unregisters the listener when the SP dies.
inline CHyprSignalListener g_onWindowOpen;
inline CHyprSignalListener g_onMouseMove;
inline CHyprSignalListener g_onFocus;
inline CHyprSignalListener g_onTick;

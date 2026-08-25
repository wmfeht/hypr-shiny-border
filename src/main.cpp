#include "globals.hpp"
#include "deco.hpp"
#include "pass.hpp"
#include "runtime.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

using namespace Hyprutils::Memory;
using namespace Desktop::View;

static CShinyBorder* shinyOn(PHLWINDOW window) {
    for (auto& d : window->m_windowDecorations) {
        if (auto* shiny = dynamic_cast<CShinyBorder*>(d.get()))
            return shiny;
    }
    return nullptr;
}

static void attach(PHLWINDOW window) {
    if (!validMapped(window) || shinyOn(window))
        return;
    HyprlandAPI::addWindowDecoration(PHANDLE, window, makeUnique<CShinyBorder>(window));
}

static void onMouseMove() {
    if (!g_cfg.enabled->value())
        return;

    const auto cursor = g_pInputManager->getMouseCoordsInternal();

    for (auto& w : Desktop::windowState()->windows()) {
        if (!validMapped(w) || w->isHidden())
            continue;

        if (g_cfg.activeOnly->value() && w != Desktop::focusState()->window())
            continue;

        auto* deco = shinyOn(w);
        if (!deco)
            continue;

        const auto mon = w->m_monitor.lock();
        if (!mon)
            continue;

        // Same space as SData.pointer (monitor-local, scaled) vs window center.
        const auto  pointer = (cursor + w->m_floatingOffset - mon->m_position) * mon->m_scale;
        const auto  center  = (w->middle() + w->m_floatingOffset - mon->m_position) * mon->m_scale;
        const float heading = shinyGpuHeading(sc<float>(pointer.x), sc<float>(pointer.y), sc<float>(center.x),
                                              sc<float>(center.y));
        const float next    = shinyQuantizeHeading(heading, sc<int>(g_cfg.angleOffset->value()),
                                                   sc<int>(g_cfg.quantizeDeg->value()));

        if (!shinyShouldDamageHeading(deco->angle(), next))
            continue;

        deco->setAngle(next);
        deco->damageEntire();
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE,
                                     "[shiny-border] Header/compositor hash mismatch. Rebuild against this Hyprland.",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 8000);
        throw std::runtime_error("[shiny-border] version mismatch");
    }

    // ARGB ints. Defaults match Omarchy's two-stop active gradient.
    g_cfg.enabled      = makeShared<Config::Values::CBoolValue>("plugin:shiny-border:enabled", "Master switch", true);
    g_cfg.activeOnly   = makeShared<Config::Values::CBoolValue>("plugin:shiny-border:active_only", "Only the focused window tracks the cursor", true);
    g_cfg.pulse        = makeShared<Config::Values::CBoolValue>("plugin:shiny-border:pulse", "Oscillate highlight width and thickness", true);
    g_cfg.quantizeDeg  = makeShared<Config::Values::CIntValue>("plugin:shiny-border:quantize_deg", "Snap angle to this many degrees", 1,
                                                               Config::Values::SIntValueOptions{.min = 1, .max = 45});
    g_cfg.angleOffset  = makeShared<Config::Values::CIntValue>("plugin:shiny-border:angle_offset", "Added to atan2 result, degrees", 0,
                                                               Config::Values::SIntValueOptions{.min = -180, .max = 180});
    g_cfg.borderSize   = makeShared<Config::Values::CIntValue>("plugin:shiny-border:border_size", "Border px, -1 = general:border_size", 3,
                                                               Config::Values::SIntValueOptions{.min = -1, .max = 20});
    g_cfg.pulseHz      = makeShared<Config::Values::CFloatValue>("plugin:shiny-border:pulse_hz", "Oscillation rate", 0.4,
                                                                 Config::Values::SFloatValueOptions{.min = 0.f, .max = 4.f});
    g_cfg.lobe         = makeShared<Config::Values::CFloatValue>("plugin:shiny-border:lobe", "Highlight half-width as a fraction of the circle", 0.18,
                                                                Config::Values::SFloatValueOptions{.min = 0.04, .max = 0.5});
    g_cfg.colA         = makeShared<Config::Values::CColorValue>("plugin:shiny-border:col.a", "Highlight head (ARGB)", 0xee33ccff);
    g_cfg.colB         = makeShared<Config::Values::CColorValue>("plugin:shiny-border:col.b", "Highlight shoulder (ARGB)", 0xee00ff99);

    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.enabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.activeOnly);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.pulse);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.quantizeDeg);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.angleOffset);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.borderSize);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.pulseHz);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.lobe);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.colA);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfg.colB);

    HyprlandAPI::reloadConfig();

    // One handle, plugin-owned. ~CConfigValue erases from the compositor
    // registry; PLUGIN_EXIT resets this before dlclose.
    g_cfg.generalBorderSize.emplace(kGeneralBorderSizeKey);

    g_onWindowOpen = Event::bus()->m_events.window.open.listen([](PHLWINDOW w) { attach(w); });
    g_onMouseMove  = Event::bus()->m_events.input.mouse.move.listen([] { onMouseMove(); });
    g_onFocus      = Event::bus()->m_events.window.active.listen([](PHLWINDOW, Desktop::eFocusReason) {
        for (auto& w : Desktop::windowState()->windows()) {
            if (auto* d = shinyOn(w)) {
                d->syncPulse();
                d->damageEntire();
            }
        }
    });

    for (auto& w : Desktop::windowState()->windows())
        attach(w);

    HyprlandAPI::addNotification(PHANDLE, "[shiny-border] tracking the mouse. try not to crash the nest.",
                                 CHyprColor{0.2, 1.0, 0.6, 1.0}, 4000);

    return {"hypr-shiny-border", "Gradient window border that faces the cursor", "wmfeht", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Event::bus() listeners are not plugin-API callbacks. Hyprland will not
    // drop them for us — the SP must die before dlclose or the next mouse.move
    // jumps into unmapped .so text.
    g_onWindowOpen.reset();
    g_onMouseMove.reset();
    g_onFocus.reset();

    // Leftover draw() must not compile a new program into a dying .so.
    markShinyTeardown();

    // Leftover pass is already-rendered dead data. clear() destroys nested
    // CTransformedWindowPassElement → nested CShinyPassElement before dlclose.
    // removeAllOfType("CShinyPassElement") does not recurse.
    // Do not remove CBorderPassElement: that is the stock border too.
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.clear();

    destroyShinyShader();

    // Destroy before dlclose so a later config reload cannot flushCaches()
    // through a pointer into unmapped plugin text.
    g_cfg.generalBorderSize.reset();
}

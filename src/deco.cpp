#include "deco.hpp"
#include "globals.hpp"
#include "runtime.hpp"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include "pass.hpp"

using namespace Hyprutils::Memory;
using namespace Desktop::View;

CShinyBorder::CShinyBorder(PHLWINDOW window) : IHyprWindowDecoration(window), m_window(window) {
    m_lastPos  = window->position(IGeometric::GEOMETRIC_CURRENT);
    m_lastSize = window->size(IGeometric::GEOMETRIC_CURRENT);
    syncPulse();
}

CShinyBorder::~CShinyBorder() {
    if (!m_pulseTimer)
        return;
    m_pulseTimer->cancel();
    if (g_pEventLoopManager)
        g_pEventLoopManager->removeTimer(m_pulseTimer);
    m_pulseTimer.reset();
}

bool CShinyBorder::pulseWanted() const {
    const auto PWINDOW = m_window.lock();
    const bool focused = PWINDOW && PWINDOW == Desktop::focusState()->window();
    return shinyPulseShouldRun(g_cfg.enabled->value(), g_cfg.pulse->value(), g_cfg.activeOnly->value(), focused);
}

void CShinyBorder::startPulse() {
    if (!g_pEventLoopManager)
        return;

    const auto period = std::chrono::milliseconds(shinyPulseTickMs(sc<float>(g_cfg.pulseHz->value())));

    if (!m_pulseTimer) {
        m_pulseTimer = makeShared<CEventLoopTimer>(
            period, [this](SP<CEventLoopTimer> self, void*) { onPulseTick(self); }, nullptr);
        g_pEventLoopManager->addTimer(m_pulseTimer);
        damageEntire();
        return;
    }

    if (!m_pulseTimer->armed()) {
        m_pulseTimer->updateTimeout(period);
        damageEntire();
    }
}

void CShinyBorder::stopPulse() {
    if (!m_pulseTimer)
        return;
    // Disarm only — cancel() is sticky and would prevent a later re-arm.
    m_pulseTimer->updateTimeout(std::nullopt);
}

void CShinyBorder::onPulseTick(SP<CEventLoopTimer> self) {
    if (!validMapped(m_window)) {
        stopPulse();
        return;
    }
    if (!pulseWanted()) {
        stopPulse();
        damageEntire();
        return;
    }
    damageEntire();
    if (self)
        self->updateTimeout(std::chrono::milliseconds(shinyPulseTickMs(sc<float>(g_cfg.pulseHz->value()))));
}

void CShinyBorder::syncPulse() {
    if (pulseWanted()) {
        startPulse();
        return;
    }
    const bool wasArmed = m_pulseTimer && m_pulseTimer->armed();
    stopPulse();
    if (wasArmed)
        damageEntire();
}

int CShinyBorder::borderSize() const {
    const int configured = sc<int>(g_cfg.borderSize->value());
    int       general    = 0;
    if (g_cfg.generalBorderSize && g_cfg.generalBorderSize->good())
        general = sc<int>(*g_cfg.generalBorderSize.value());
    return shinyResolvedBorderSize(configured, general);
}

SDecorationPositioningInfo CShinyBorder::getPositioningInfo() {
    const int bs = borderSize();
    m_extents    = {{bs, bs}, {bs, bs}};

    SDecorationPositioningInfo info;
    info.policy         = DECORATION_POSITION_STICKY;
    info.edges          = DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP;
    info.reserved       = true;
    info.priority       = 9990; // stock border is 10000; we sit just inside its claim
    info.desiredExtents = m_extents;
    return info;
}

void CShinyBorder::onPositioningReply(const SDecorationPositioningReply& reply) {
    m_assignedGeometry = reply.assignedGeometry;
}

CBox CShinyBorder::assignedBoxGlobal() {
    if (!shinyCanUseMappedGeometry(validMapped(m_window), static_cast<bool>(g_pHyprRenderer)))
        return {};

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return {};

    CBox box = m_assignedGeometry;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(
        DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, m_window));

    if (!PWINDOW->m_workspace)
        return box;

    if (!PWINDOW->m_pinned)
        box.translate(PWINDOW->m_workspace->m_renderOffset->value());

    return box;
}

void CShinyBorder::draw(PHLMONITOR pMonitor, float const& a) {
    if (!validMapped(m_window)) {
        stopPulse();
        return;
    }

    syncPulse();

    if (!g_cfg.enabled->value())
        return;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return;

    if (g_cfg.activeOnly->value() && PWINDOW != Desktop::focusState()->window())
        return;

    const int BORDERSIZE = borderSize();
    if (BORDERSIZE <= 0)
        return;

    if (m_assignedGeometry.width < m_extents.topLeft.x + 1 || m_assignedGeometry.height < m_extents.topLeft.y + 1)
        return;

    CBox outerBox = assignedBoxGlobal()
                        .translate(-pMonitor->m_position + PWINDOW->m_floatingOffset)
                        .scale(pMonitor->m_scale)
                        .round();

    if (outerBox.width < 1 || outerBox.height < 1)
        return;

    const ShinyDrawShared shared{
        .rounding      = sc<int>(PWINDOW->rounding() * pMonitor->m_scale),
        .outerRound    = sc<int>((PWINDOW->rounding() + BORDERSIZE) * pMonitor->m_scale),
        .roundingPower = PWINDOW->roundingPower(),
        .a             = a,
        .borderSize    = BORDERSIZE,
        .colA          = sc<uint64_t>(g_cfg.colA->value()),
        .colB          = sc<uint64_t>(g_cfg.colB->value()),
    };
    const auto mapped = shinyMapDrawBackends(shared, pMonitor->m_scale);

    if (ensureShinyShader()) {
        const auto cursor = g_pInputManager->getMouseCoordsInternal();
        CShinyPassElement::SData data;
        data.shared  = mapped.shader;
        data.box     = outerBox;
        data.pointer = (cursor + PWINDOW->m_floatingOffset - pMonitor->m_position) * pMonitor->m_scale;
        const auto pulseU = shinyPulseUniforms(g_cfg.pulse->value(), g_pHyprRenderer->m_globalTimer.getSeconds(),
                                               sc<float>(g_cfg.pulseHz->value()));
        data.time         = pulseU.time;
        data.pulseHz      = pulseU.pulseHz;
        data.lobe    = sc<float>(g_cfg.lobe->value());
        g_pHyprRenderer->addPassElement(makeUnique<CShinyPassElement>(data));
        return;
    }

    CBox windowBox = outerBox.copy().expand(-mapped.fallback.expandPx).round();
    Config::CGradientValueData grad(
        {CHyprColor{mapped.fallback.shared.colA}, CHyprColor{mapped.fallback.shared.colB}}, m_angle);

    CBorderPassElement::SBorderData data;
    data.box           = windowBox;
    data.grad1         = grad;
    data.round         = mapped.fallback.shared.rounding;
    data.outerRound    = mapped.fallback.shared.outerRound;
    data.roundingPower = mapped.fallback.shared.roundingPower;
    data.a             = mapped.fallback.shared.a;
    data.borderSize    = mapped.fallback.shared.borderSize;
    data.window        = m_window;

    g_pHyprRenderer->addPassElement(makeUnique<CBorderPassElement>(data));
}

eDecorationType CShinyBorder::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CShinyBorder::updateWindow(PHLWINDOW pWindow) {
    const auto pos  = pWindow->position(IGeometric::GEOMETRIC_CURRENT);
    const auto size = pWindow->size(IGeometric::GEOMETRIC_CURRENT);
    const int  bs   = borderSize();

    const auto actions = shinyUpdateWindowActions(
        ShinyGeoLatch{pos.x, pos.y, size.x, size.y}, bs,
        ShinyGeoLatch{m_lastPos.x, m_lastPos.y, m_lastSize.x, m_lastSize.y}, m_lastSizeB);

    if (actions.reposition) {
        m_lastSizeB = bs;
        g_pDecorationPositioner->repositionDeco(this);
    }

    m_lastPos  = pos;
    m_lastSize = size;

    if (actions.damage)
        damageEntire();

    syncPulse();
}

void CShinyBorder::damageEntire() {
    const bool mapped   = validMapped(m_window);
    const bool renderer = static_cast<bool>(g_pHyprRenderer);
    // mapped + renderer first — fullscreen lookup is not safe on an unmapped
    // / closing window, and renderer damage is not safe with a null renderer.
    if (!shinyCanUseMappedGeometry(mapped, renderer))
        return;

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW)
        return;

    // skip exclusive fullscreen — renderWindow already sets decorate = false
    // for that mode
    const bool exclusiveFs =
        Fullscreen::controller()->getFullscreenModes(PWINDOW).internal == Fullscreen::FSMODE_FULLSCREEN;
    if (!shinyCanDamage(mapped, renderer, exclusiveFs))
        return;

    CBox dm = assignedBoxGlobal();
    if (dm.w <= 0 || dm.h <= 0)
        return;

    const int pad = std::max(borderSize() + 6, 8);
    CRegion   rg{dm.copy().expand(2)};
    CBox      hole = dm.copy().expand(-pad);
    if (hole.w > 1 && hole.h > 1)
        rg.subtract(hole);
    g_pHyprRenderer->damageRegion(rg);
}

eDecorationLayer CShinyBorder::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CShinyBorder::getDecorationFlags() {
    return DECORATION_PART_OF_MAIN_WINDOW;
}

std::string CShinyBorder::getDisplayName() {
    return "Shiny Border";
}

void CShinyBorder::setAngle(float radians) {
    m_angle = radians;
}

float CShinyBorder::angle() const {
    return m_angle;
}

#include "deco.hpp"
#include "globals.hpp"

#include <algorithm>
#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/DecorationPositioner.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include "pass.hpp"

using namespace Hyprutils::Memory;
using namespace Desktop::View;

CShinyBorder::CShinyBorder(PHLWINDOW window) : IHyprWindowDecoration(window), m_window(window) {
    m_lastPos  = window->position(IGeometric::GEOMETRIC_CURRENT);
    m_lastSize = window->size(IGeometric::GEOMETRIC_CURRENT);
}

int CShinyBorder::borderSize() const {
    const int configured = sc<int>(g_cfg.borderSize->value());
    if (configured >= 0)
        return configured;

    static auto PBORDERSIZE = CConfigValue<Config::INTEGER>("general:border_size");
    return sc<int>(*PBORDERSIZE);
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
    CBox box = m_assignedGeometry;
    box.translate(g_pDecorationPositioner->getEdgeDefinedPoint(
        DECORATION_EDGE_BOTTOM | DECORATION_EDGE_LEFT | DECORATION_EDGE_RIGHT | DECORATION_EDGE_TOP, m_window));

    const auto PWINDOW = m_window.lock();
    if (!PWINDOW || !PWINDOW->m_workspace)
        return box;

    if (!PWINDOW->m_pinned)
        box.translate(PWINDOW->m_workspace->m_renderOffset->value());

    return box;
}

void CShinyBorder::draw(PHLMONITOR pMonitor, float const& a) {
    if (!validMapped(m_window) || !g_cfg.enabled->value())
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

    const auto ROUNDING      = PWINDOW->rounding() * pMonitor->m_scale;
    const auto ROUNDINGPOWER = PWINDOW->roundingPower();
    const auto OUTERROUND    = (PWINDOW->rounding() + BORDERSIZE) * pMonitor->m_scale;
    const auto BS_PX         = sc<int>(std::round(BORDERSIZE * pMonitor->m_scale));

    if (ensureShinyShader()) {
        const auto cursor = g_pInputManager->getMouseCoordsInternal();
        CShinyPassElement::SData data;
        data.box           = outerBox;
        data.colA          = CHyprColor{sc<uint64_t>(g_cfg.colA->value())};
        data.colB          = CHyprColor{sc<uint64_t>(g_cfg.colB->value())};
        data.pointer       = (cursor + PWINDOW->m_floatingOffset - pMonitor->m_position) * pMonitor->m_scale;
        data.angle         = m_angle;
        data.a             = a;
        data.roundingPower = ROUNDINGPOWER;
        data.time          = g_cfg.pulse->value() ? g_pHyprRenderer->m_globalTimer.getSeconds() : 0.f;
        data.pulseHz       = g_cfg.pulse->value() ? sc<float>(g_cfg.pulseHz->value()) : 0.f;
        data.lobe          = sc<float>(g_cfg.lobe->value());
        data.round         = sc<int>(ROUNDING);
        data.outerRound    = sc<int>(OUTERROUND);
        data.borderSize    = BS_PX;
        g_pHyprRenderer->addPassElement(makeUnique<CShinyPassElement>(data));
        return;
    }

    CBox windowBox = outerBox.copy().expand(-BS_PX).round();
    Config::CGradientValueData grad(
        {CHyprColor{sc<uint64_t>(g_cfg.colA->value())}, CHyprColor{sc<uint64_t>(g_cfg.colB->value())}}, m_angle);

    CBorderPassElement::SBorderData data;
    data.box           = windowBox;
    data.grad1         = grad;
    data.round         = sc<int>(ROUNDING);
    data.outerRound    = sc<int>(OUTERROUND);
    data.roundingPower = ROUNDINGPOWER;
    data.a             = a;
    data.borderSize    = BORDERSIZE;
    data.window        = m_window;

    g_pHyprRenderer->addPassElement(makeUnique<CBorderPassElement>(data));
}

eDecorationType CShinyBorder::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CShinyBorder::updateWindow(PHLWINDOW pWindow) {
    m_lastPos  = pWindow->position(IGeometric::GEOMETRIC_CURRENT);
    m_lastSize = pWindow->size(IGeometric::GEOMETRIC_CURRENT);

    const int bs = borderSize();
    if (bs != m_lastSizeB) {
        m_lastSizeB = bs;
        g_pDecorationPositioner->repositionDeco(this);
    }

    damageEntire();
}

void CShinyBorder::damageEntire() {
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

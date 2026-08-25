#include "../src/runtime.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static int g_fails = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                       \
            g_fails++;                                                                                                 \
        }                                                                                                              \
    } while (0)

static std::string sourceDir() {
    std::string file            = __FILE__;
    const auto  slash           = file.find_last_of('/');
    const std::string testsDir  = (slash == std::string::npos) ? std::string(".") : file.substr(0, slash);
    return testsDir + "/../src";
}

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static std::string functionBody(const std::string& src, const std::string& signature) {
    const auto pos = src.find(signature);
    if (pos == std::string::npos)
        return {};
    const auto brace = src.find('{', pos);
    if (brace == std::string::npos)
        return {};
    int depth = 0;
    for (size_t i = brace; i < src.size(); ++i) {
        if (src[i] == '{')
            depth++;
        else if (src[i] == '}') {
            depth--;
            if (depth == 0)
                return src.substr(brace, i - brace + 1);
        }
    }
    return {};
}

static int countNeedle(const std::string& hay, const std::string& needle) {
    int    n = 0;
    size_t p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) {
        n++;
        p += needle.size();
    }
    return n;
}

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
}

static void checkPulseDecisions() {
    // pulse false → should not run, regardless of focus / active_only.
    CHECK(!shinyPulseShouldRun(true, false, true, true));
    CHECK(!shinyPulseShouldRun(true, false, true, false));
    CHECK(!shinyPulseShouldRun(true, false, false, true));
    CHECK(!shinyPulseShouldRun(true, false, false, false));

    // pulse true + active_only true + not focused → should not run; focused → should run.
    CHECK(!shinyPulseShouldRun(true, true, true, false));
    CHECK(shinyPulseShouldRun(true, true, true, true));

    // pulse true + active_only false → should run for focused and unfocused.
    CHECK(shinyPulseShouldRun(true, true, false, true));
    CHECK(shinyPulseShouldRun(true, true, false, false));

    // plugin enabled false → should not run.
    CHECK(!shinyPulseShouldRun(false, true, true, true));
    CHECK(!shinyPulseShouldRun(false, true, false, false));
    CHECK(!shinyPulseShouldRun(false, false, false, true));

    // pulse false → uniforms time = 0 and pulseHz = 0 even when clock and Hz are non-zero.
    const auto off = shinyPulseUniforms(false, 12.5f, 0.4f);
    CHECK(off.time == 0.f);
    CHECK(off.pulseHz == 0.f);
    const auto offOther = shinyPulseUniforms(false, 3.f, 4.f);
    CHECK(offOther.time == 0.f);
    CHECK(offOther.pulseHz == 0.f);

    // pulse true → uniforms are that clock and that Hz.
    const auto on = shinyPulseUniforms(true, 12.5f, 0.4f);
    CHECK(on.time == 12.5f);
    CHECK(on.pulseHz == 0.4f);
    const auto onFast = shinyPulseUniforms(true, 0.25f, 4.f);
    CHECK(onFast.time == 0.25f);
    CHECK(onFast.pulseHz == 4.f);

    // Re-arm period is not a once-per-cycle 1/Hz fire (that hitchs vs compositor time).
    CHECK(shinyPulseTickMs(0.4f) > 0);
    CHECK(shinyPulseTickMs(0.4f) < static_cast<int>(1000.f / 0.4f));
    CHECK(shinyPulseTickMs(4.f) > 0);
    CHECK(shinyPulseTickMs(4.f) < static_cast<int>(1000.f / 4.f));
}

static void checkUpdateWindowActions() {
    const ShinyGeoLatch geo{10.0, 20.0, 100.0, 200.0};
    const int           bs = 3;

    // No geo change + no border change → no reposition, no damage.
    auto a = shinyUpdateWindowActions(geo, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(!a.damage);

    // Geo change + same border → no reposition, damage.
    const ShinyGeoLatch moved{11.0, 20.0, 100.0, 200.0};
    a = shinyUpdateWindowActions(moved, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(a.damage);

    const ShinyGeoLatch resized{10.0, 20.0, 100.0, 201.0};
    a = shinyUpdateWindowActions(resized, bs, geo, bs);
    CHECK(!a.reposition);
    CHECK(a.damage);

    // Same geo + border change → reposition and damage.
    a = shinyUpdateWindowActions(geo, 4, geo, bs);
    CHECK(a.reposition);
    CHECK(a.damage);

    // Both change → reposition and damage.
    a = shinyUpdateWindowActions(moved, 4, geo, bs);
    CHECK(a.reposition);
    CHECK(a.damage);
}

static void checkProductionWiring() {
    const std::string src  = sourceDir();
    const std::string deco    = readFile(src + "/deco.cpp");
    const std::string decoHpp = readFile(src + "/deco.hpp");
    const std::string pass    = readFile(src + "/pass.cpp");
    const std::string main    = readFile(src + "/main.cpp");
    const std::string globals = readFile(src + "/globals.hpp");

    CHECK(!deco.empty());
    CHECK(!decoHpp.empty());
    CHECK(!pass.empty());
    CHECK(!main.empty());
    CHECK(!globals.empty());

    const auto assigned = functionBody(deco, "CShinyBorder::assignedBoxGlobal");
    const auto damage   = functionBody(deco, "CShinyBorder::damageEntire");
    const auto update   = functionBody(deco, "void CShinyBorder::updateWindow");
    const auto display  = functionBody(deco, "std::string CShinyBorder::getDisplayName");
    const auto shinyOn  = functionBody(main, "static CShinyBorder* shinyOn");
    const auto attach   = functionBody(main, "static void attach");
    const auto draw     = functionBody(pass, "CShinyPassElement::draw");

    CHECK(!assigned.empty());
    CHECK(!damage.empty());
    CHECK(!update.empty());
    CHECK(!display.empty());
    CHECK(!shinyOn.empty());
    CHECK(!attach.empty());
    CHECK(!draw.empty());

    // One decoration walk. Identity is this plugin's CShinyBorder RTTI, not
    // the display-name string. windowHasShiny is gone.
    CHECK(main.find("windowHasShiny") == std::string::npos);
    CHECK(shinyOn.find("dynamic_cast<CShinyBorder*>") != std::string::npos);
    CHECK(shinyOn.find("getDisplayName") == std::string::npos);
    CHECK(shinyOn.find("Shiny Border") == std::string::npos);
    CHECK(countNeedle(shinyOn, "dynamic_cast<CShinyBorder*>") == 1);
    CHECK(attach.find("getDisplayName") == std::string::npos);
    CHECK(attach.find("Shiny Border") == std::string::npos);
    CHECK(main.find("getDisplayName") == std::string::npos);

    // Attach skips when not valid-mapped or the deco is already present.
    CHECK(attach.find("validMapped(window)") != std::string::npos);
    CHECK(attach.find("shinyOn(window)") != std::string::npos);
    CHECK(countNeedle(attach, "shinyOn") == 1);
    const auto attachMapped = attach.find("validMapped(window)");
    const auto attachShiny  = attach.find("shinyOn(window)");
    const auto attachRet    = attach.find("return");
    const auto attachAdd    = attach.find("addWindowDecoration");
    CHECK(attachMapped < attachRet);
    CHECK(attachShiny < attachRet);
    CHECK(attachRet < attachAdd);

    // Display name stays for Hyprland UI; it is not the attach/lookup key.
    CHECK(display.find("Shiny Border") != std::string::npos);
    CHECK(decoHpp.find("getDisplayName") != std::string::npos);

    // updateWindow drives the shipped dirty-check. Reposition only on the
    // border-size-changed path; damage is not unconditional.
    CHECK(update.find("shinyUpdateWindowActions") != std::string::npos);
    CHECK(update.find("ShinyGeoLatch{pos.x, pos.y, size.x, size.y}, bs") != std::string::npos);
    CHECK(update.find("ShinyGeoLatch{m_lastPos.x, m_lastPos.y, m_lastSize.x, m_lastSize.y}, m_lastSizeB") !=
          std::string::npos);
    CHECK(countNeedle(update, "repositionDeco") == 1);
    CHECK(update.find("if (actions.reposition)") != std::string::npos);
    CHECK(update.find("if (actions.reposition)") < update.find("repositionDeco"));
    CHECK(countNeedle(update, "damageEntire()") == 1);
    CHECK(update.find("if (actions.damage)") != std::string::npos);
    CHECK(update.find("if (actions.damage)") < update.find("damageEntire()"));
    CHECK(update.find("syncPulse()") != std::string::npos);

    // Last pos/size are read (passed into the helper) before they are written.
    const auto lastPosRead   = update.find("m_lastPos.");
    const auto lastSizeRead  = update.find("m_lastSize.");
    const auto lastBorderArg = update.find("m_lastSizeB)");
    const auto helperAt      = update.find("shinyUpdateWindowActions");
    const auto lastPosWrite  = update.find("m_lastPos  =");
    const auto lastSizeWrite = update.find("m_lastSize =");
    const auto lastBWrite    = update.find("m_lastSizeB =");
    CHECK(lastPosRead != std::string::npos);
    CHECK(lastSizeRead != std::string::npos);
    CHECK(lastBorderArg != std::string::npos);
    CHECK(lastPosWrite != std::string::npos);
    CHECK(lastSizeWrite != std::string::npos);
    CHECK(lastBWrite != std::string::npos);
    CHECK(helperAt < lastPosWrite);
    CHECK(lastPosRead < lastPosWrite);
    CHECK(lastSizeRead < lastSizeWrite);
    CHECK(lastBorderArg < lastBWrite);
    CHECK(decoHpp.find("m_lastPos") != std::string::npos);
    CHECK(decoHpp.find("m_lastSize") != std::string::npos);
    CHECK(decoHpp.find("m_lastSizeB") != std::string::npos);

    // assignedBoxGlobal: validMapped + g_pHyprRenderer (via the shipped gate)
    // before getEdgeDefinedPoint. Lock before the positioner call.
    CHECK(assigned.find("shinyCanUseMappedGeometry") != std::string::npos);
    CHECK(assigned.find("validMapped") != std::string::npos);
    CHECK(assigned.find("g_pHyprRenderer") != std::string::npos);
    CHECK(assigned.find("getEdgeDefinedPoint") != std::string::npos);
    CHECK(assigned.find(".lock()") != std::string::npos);

    const auto aMapped   = assigned.find("validMapped");
    const auto aRenderer = assigned.find("g_pHyprRenderer");
    const auto aGate     = assigned.find("shinyCanUseMappedGeometry");
    const auto aLock     = assigned.find(".lock()");
    const auto aEdge     = assigned.find("getEdgeDefinedPoint");
    CHECK(aGate < aEdge);
    CHECK(aMapped < aEdge);
    CHECK(aRenderer < aEdge);
    CHECK(aLock < aEdge);
    CHECK(aMapped < aLock);
    CHECK(aRenderer < aLock);
    const auto aRet = assigned.find("return {}");
    CHECK(aRet != std::string::npos);
    CHECK(aRet < aEdge);

    // damageEntire: mapped + renderer before fullscreen lookup, then
    // FSMODE_FULLSCREEN, then damageRegion. Do not start after the guard.
    CHECK(damage.find("shinyCanUseMappedGeometry") != std::string::npos);
    CHECK(damage.find("shinyCanDamage") != std::string::npos);
    CHECK(damage.find("validMapped") != std::string::npos);
    CHECK(damage.find("g_pHyprRenderer") != std::string::npos);
    CHECK(damage.find("getFullscreenModes") != std::string::npos);
    CHECK(damage.find("FSMODE_FULLSCREEN") != std::string::npos);
    CHECK(damage.find("== Fullscreen::FSMODE_FULLSCREEN") != std::string::npos);
    CHECK(damage.find("damageRegion") != std::string::npos);

    const auto dMapped   = damage.find("validMapped");
    const auto dRenderer = damage.find("g_pHyprRenderer");
    const auto dGeo      = damage.find("shinyCanUseMappedGeometry");
    const auto dFsLookup = damage.find("getFullscreenModes");
    const auto dFsMode   = damage.find("FSMODE_FULLSCREEN");
    const auto dCan      = damage.find("shinyCanDamage");
    const auto dRegion   = damage.find("damageRegion");
    CHECK(dGeo < dFsLookup);
    CHECK(dMapped < dFsLookup);
    CHECK(dRenderer < dFsLookup);
    const auto dRet = damage.find("return", dGeo);
    CHECK(dRet != std::string::npos);
    CHECK(dRet < dFsLookup);
    CHECK(dFsLookup < dRegion);
    CHECK(dFsMode < dRegion);
    CHECK(dCan < dRegion);
    CHECK(dMapped < dRegion);
    CHECK(dRenderer < dRegion);

    // shader draw: read SHADER_SHADER_VAO, skip bind when the shipped gate
    // refuses, never pass getUniformLocation straight into glBindVertexArray.
    CHECK(draw.find("getUniformLocation(SHADER_SHADER_VAO)") != std::string::npos);
    CHECK(draw.find("shinyCanBindVao") != std::string::npos);
    CHECK(draw.find("glBindVertexArray(shader->getUniformLocation") == std::string::npos);

    const auto vaoRead = draw.find("getUniformLocation(SHADER_SHADER_VAO)");
    const auto vaoGate = draw.find("shinyCanBindVao");
    const auto vaoBind = draw.find("glBindVertexArray");
    CHECK(vaoRead < vaoGate);
    CHECK(vaoGate < vaoBind);
    const auto retAfterGate = draw.find("return {}", vaoGate);
    CHECK(retAfterGate != std::string::npos);
    CHECK(retAfterGate < vaoBind);

    // No process-wide pulse tick. g_onTick / render.pre are gone; PLUGIN_EXIT
    // does not reset a tick listener. Pulse is per-deco.
    CHECK(main.find("g_onTick") == std::string::npos);
    CHECK(main.find("render.pre") == std::string::npos);
    CHECK(globals.find("g_onTick") == std::string::npos);
    CHECK(globals.find("render.pre") == std::string::npos);

    const auto exitFn = functionBody(main, "void PLUGIN_EXIT");
    CHECK(!exitFn.empty());
    CHECK(exitFn.find("g_onTick") == std::string::npos);
    CHECK(exitFn.find("g_onTick.reset()") == std::string::npos);
    CHECK(exitFn.find("g_onFocus.reset()") != std::string::npos);
    CHECK(exitFn.find("g_onMouseMove.reset()") != std::string::npos);
    CHECK(exitFn.find("g_onWindowOpen.reset()") != std::string::npos);

    // window.active starts/stops pulse (and still damageEntire's through the
    // phase-2 guard). The guard is not a listener filter — do not add validMapped.
    const auto focusAt = main.find("window.active");
    CHECK(focusAt != std::string::npos);
    const auto focusEnd = main.find("});", focusAt);
    CHECK(focusEnd != std::string::npos);
    const auto focus = main.substr(focusAt, focusEnd - focusAt);
    CHECK(focus.find("windows()") != std::string::npos);
    CHECK(focus.find("syncPulse") != std::string::npos);
    CHECK(focus.find("damageEntire") != std::string::npos);
    CHECK(focus.find("validMapped") == std::string::npos);
    CHECK(focus.find("render.pre") == std::string::npos);

    // Per-deco timer (not a process-wide tick) damageEntire's; draw zeros
    // uniforms via the shipped helper; destructor drops the timer.
    CHECK(decoHpp.find("CEventLoopTimer") != std::string::npos);
    CHECK(decoHpp.find("m_pulseTimer") != std::string::npos);
    CHECK(decoHpp.find("syncPulse") != std::string::npos);
    CHECK(deco.find("CAnimatedVariable") == std::string::npos);

    const auto dtor = functionBody(deco, "CShinyBorder::~CShinyBorder");
    CHECK(!dtor.empty());
    CHECK(dtor.find("cancel()") != std::string::npos);
    CHECK(dtor.find("removeTimer") != std::string::npos);
    CHECK(dtor.find("m_pulseTimer.reset()") != std::string::npos);

    const auto tick = functionBody(deco, "void CShinyBorder::onPulseTick");
    CHECK(!tick.empty());
    CHECK(tick.find("damageEntire()") != std::string::npos);
    CHECK(tick.find("validMapped") != std::string::npos);
    CHECK(tick.find("pulseWanted") != std::string::npos);
    CHECK(tick.find("updateTimeout") != std::string::npos);
    const auto tickMapped = tick.find("validMapped");
    const auto tickDamage = tick.find("damageEntire()");
    CHECK(tickMapped < tickDamage);

    const auto start = functionBody(deco, "void CShinyBorder::startPulse");
    CHECK(!start.empty());
    CHECK(start.find("makeShared<CEventLoopTimer>") != std::string::npos);
    CHECK(start.find("addTimer") != std::string::npos);
    CHECK(start.find("damageEntire()") != std::string::npos);
    CHECK(start.find("shinyPulseTickMs") != std::string::npos);

    const auto sync = functionBody(deco, "void CShinyBorder::syncPulse");
    CHECK(!sync.empty());
    CHECK(sync.find("pulseWanted") != std::string::npos);
    CHECK(sync.find("startPulse") != std::string::npos);
    CHECK(sync.find("stopPulse") != std::string::npos);
    CHECK(sync.find("damageEntire()") != std::string::npos);

    const auto wanted = functionBody(deco, "bool CShinyBorder::pulseWanted");
    CHECK(!wanted.empty());
    CHECK(wanted.find("shinyPulseShouldRun") != std::string::npos);

    const auto decoDraw = functionBody(deco, "void CShinyBorder::draw");
    CHECK(!decoDraw.empty());
    CHECK(decoDraw.find("shinyPulseUniforms") != std::string::npos);
    CHECK(decoDraw.find("m_globalTimer") != std::string::npos);
    CHECK(decoDraw.find("syncPulse()") != std::string::npos);
    const auto uniAt   = decoDraw.find("shinyPulseUniforms");
    const auto clockAt = decoDraw.find("m_globalTimer");
    const auto timeAt  = decoDraw.find("data.time");
    CHECK(uniAt < timeAt);
    CHECK(clockAt < timeAt);
}

int main() {
    checkShippedDecisions();
    checkPulseDecisions();
    checkUpdateWindowActions();
    checkProductionWiring();

    if (g_fails) {
        std::fprintf(stderr, "%d checks failed\n", g_fails);
        return 1;
    }
    std::puts("ok");
    return 0;
}

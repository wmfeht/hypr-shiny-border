#include "pass.hpp"
#include "teardown.hpp"
#include "runtime.hpp"
#include "shaders.hpp"
#include "globals.hpp"

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Color.hpp>

#include <GLES3/gl32.h>

using namespace Render::GL;

static SP<CShader> g_shinyShader;

static bool hyprGlAlive() {
    return static_cast<bool>(g_pHyprOpenGL);
}

static void hyprMakeCurrent() {
    g_pHyprOpenGL->makeEGLCurrent();
}

static bool hyprShaderLive() {
    return g_shinyShader && g_shinyShader->program();
}

static bool hyprCompileShader() {
    if (!g_pHyprOpenGL)
        return false;

    g_pHyprOpenGL->makeEGLCurrent();
    g_shinyShader = makeShared<CShader>();
    if (!g_shinyShader->createProgram(SHINY_VERT, SHINY_FRAG, true, false)) {
        Log::logger->log(Log::ERR, "[shiny-border] fragment shader failed to compile");
        g_shinyShader.reset();
        return false;
    }

    return true;
}

static void hyprResetShader() {
    g_shinyShader.reset();
}

[[gnu::constructor]] static void bindShinyShaderOps() {
    shinySetShaderOps({
        .glAlive     = hyprGlAlive,
        .makeCurrent = hyprMakeCurrent,
        .shaderLive  = hyprShaderLive,
        .compile     = hyprCompileShader,
        .reset       = hyprResetShader,
    });
}

CShinyPassElement::CShinyPassElement(const SData& data) : m_data(data) {}

bool CShinyPassElement::needsLiveBlur() {
    return false;
}

bool CShinyPassElement::needsPrecomputeBlur() {
    return false;
}

bool CShinyPassElement::disableSimplification() {
    return true;
}

std::optional<CBox> CShinyPassElement::boundingBox() {
    if (!g_pHyprRenderer->m_renderData.pMonitor)
        return m_data.box;
    return m_data.box.copy().scale(1.F / g_pHyprRenderer->m_renderData.pMonitor->m_scale).round();
}

CRegion CShinyPassElement::opaqueRegion() {
    return {};
}

std::vector<UP<IPassElement>> CShinyPassElement::draw() {
    if (!ensureShinyShader())
        return {};

    auto&      rd = g_pHyprRenderer->m_renderData;
    const auto mon = rd.pMonitor;
    if (!mon || m_data.box.w <= 0 || m_data.box.h <= 0)
        return {};

    CBox box = m_data.box;
    rd.renderModif.applyToBox(box);

    const auto proj = g_pHyprRenderer->projectBoxToTarget(box);

    CBox transformed = m_data.box;
    transformed.transform(Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform)), mon->m_transformedSize.x, mon->m_transformedSize.y);

    CBox ptrBox{m_data.pointer.x, m_data.pointer.y, 1, 1};
    ptrBox.transform(Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform)), mon->m_transformedSize.x, mon->m_transformedSize.y);

    g_pHyprOpenGL->blend(true);
    auto shader = g_pHyprOpenGL->useShader(g_shinyShader);
    if (!shader)
        return {};

    const CHyprColor colA{m_data.shared.colA};
    const CHyprColor colB{m_data.shared.colB};

    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, proj.getMatrix());
    shader->setUniformFloat4(SHADER_COLOR, sc<float>(colA.r), sc<float>(colA.g), sc<float>(colA.b), sc<float>(colA.a));
    shader->setUniformFloat4(SHADER_COLOR_SRGB, sc<float>(colB.r), sc<float>(colB.g), sc<float>(colB.b), sc<float>(colB.a));
    shader->setUniformFloat2(SHADER_TOP_LEFT, sc<float>(transformed.x), sc<float>(transformed.y));
    shader->setUniformFloat2(SHADER_FULL_SIZE, sc<float>(transformed.width), sc<float>(transformed.height));
    shader->setUniformFloat(SHADER_RADIUS, sc<float>(m_data.shared.rounding));
    shader->setUniformFloat(SHADER_RADIUS_OUTER, sc<float>(m_data.shared.outerRound));
    shader->setUniformFloat(SHADER_ROUNDING_POWER, m_data.shared.roundingPower);
    shader->setUniformFloat(SHADER_THICK, shinyShaderThick(sc<float>(m_data.shared.borderSize), sc<float>(mon->m_scale)));
    shader->setUniformFloat(SHADER_TIME, m_data.time);
    shader->setUniformFloat(SHADER_ALPHA, m_data.shared.a);
    shader->setUniformFloat(SHADER_RANGE, m_data.lobe);
    shader->setUniformFloat(SHADER_BRIGHTNESS, m_data.pulseHz);
    shader->setUniformFloat2(SHADER_POINTER, sc<float>(ptrBox.x), sc<float>(ptrBox.y));

    const GLint vao = shader->getUniformLocation(SHADER_SHADER_VAO);
    if (!shinyCanBindVao(vao))
        return {};
    glBindVertexArray(vao);

    const CRegion* dmg = &rd.damage;
    if (rd.clipBox.width != 0 && rd.clipBox.height != 0) {
        CRegion clip{rd.clipBox.x, rd.clipBox.y, rd.clipBox.width, rd.clipBox.height};
        clip.intersect(rd.damage);
        clip.forEachRect([](const auto& RECT) {
            g_pHyprOpenGL->scissor(&RECT, g_pHyprRenderer->m_renderData.transformDamage);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        });
    } else {
        dmg->forEachRect([](const auto& RECT) {
            g_pHyprOpenGL->scissor(&RECT, g_pHyprRenderer->m_renderData.transformDamage);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        });
    }

    glBindVertexArray(0);
    g_pHyprOpenGL->scissor(nullptr);
    return {};
}

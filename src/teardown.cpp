#include "teardown.hpp"

static bool           g_tearingDown = false;
static ShinyShaderOps g_ops;

void shinySetShaderOps(ShinyShaderOps ops) {
    g_ops = ops;
}

void markShinyTeardown() {
    g_tearingDown = true;
}

bool shinyTeardownStarted() {
    return g_tearingDown;
}

bool ensureShinyShader() {
    if (g_tearingDown)
        return false;
    if (g_ops.shaderLive && g_ops.shaderLive())
        return true;
    if (!g_ops.compile)
        return false;
    return g_ops.compile();
}

void destroyShinyShader() {
    if (!g_ops.glAlive || !g_ops.glAlive())
        return;
    if (g_ops.makeCurrent)
        g_ops.makeCurrent();
    if (g_ops.reset)
        g_ops.reset();
}

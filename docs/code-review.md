# Code review — hypr-shiny-border (initial commit)

Date: 2026-08-24
Scope: full tree at `608ea92` (`main`). Working tree was clean.
Focus: compositor crash-to-desktop (CTD) first, then correctness, then structure.

This is a Hyprland v0.56.2 plugin. A plugin crash is a session crash. The hash
check, listener reset, and top-level `removeAllOfType` are real; they do not
cover the full object graph Hyprland still holds between frames.

**Verdict:** do not treat unload / `mise run reload` as a supported inner loop
until the four CTD blockers below are fixed. Login-session load of this `.so`
is already a known risk.

---

## Blockers (CTD)

### 1. Leftover render pass is not fully destroyed before `dlclose`

`PLUGIN_EXIT` only does a surgical remove:

```cpp
// src/main.cpp PLUGIN_EXIT
g_pHyprRenderer->m_renderPass.removeAllOfType("CShinyPassElement");
```

Hyprland clears `m_renderPass` at the **start of the next** `beginRender()`,
not at the end of the current frame. That leftover pass is why
`removeAllOfType` exists at all.

That call does **not recurse**. `CShinyBorder` is `DECORATION_LAYER_OVER`. If
the window has transformers, `renderWindow()` redirects `addPassElement` into
a nested `CTransformedWindowPassElement::m_data.pass`. Those
`CShinyPassElement`s never see `removeAllOfType("CShinyPassElement")`.

Unload then becomes:

1. `PLUGIN_EXIT` — top-level shiny elements gone; nested ones remain
2. decorations stripped
3. `dlclose`
4. next `beginRender()` → `m_renderPass.clear()` → nested
   `~CShinyPassElement()` via an unmapped vtable

That is the same SIGSEGV already hit on 2026-08-24, one level down. Motion
blur / any `m_transformers` entry is enough.

`CRenderPass::m_passElements` is private, so the plugin cannot walk nested
passes. Surgical remove cannot see them.

**Fix:** in `PLUGIN_EXIT`, `g_pHyprRenderer->m_renderPass.clear()`. That
destroys `CTransformedWindowPassElement`, which destroys the nested pass,
which destroys `CShinyPassElement`, all before `dlclose`.

The leftover pass is already-rendered dead data. Next `beginRender` would
destroy it anyway. Stock `CBorderPassElement` in that leftover pass is a
compositor type and was going to die on the next `beginRender` regardless.

Do **not** `removeAllOfType("CBorderPassElement")` on a *live* pass (that is
also the stock border). Clearing the leftover pass *after a completed frame*
is a different operation.

Do this **before** `destroyShinyShader()`.

### 2. `CShader` destructor can call GL with no context

```cpp
// src/pass.cpp
void destroyShinyShader() {
    if (g_pHyprOpenGL)
        g_pHyprOpenGL->makeEGLCurrent();
    g_shinyShader.reset();
}
```

`~CShader()` always `glDeleteProgram` / `glDeleteVertexArrays` if
`m_program != 0`. If `g_pHyprOpenGL` is already null (compositor teardown,
plugin unload after GL shutdown), `reset()` still runs those deletes with no
EGL context. NVIDIA in particular will SIGSEGV here.

**Fix:** only `reset()` when `g_pHyprOpenGL` is alive and current. If GL is
already gone, leak the `SP` — the process is dying. Do this *after* the pass
is cleared so no `draw()` can resurrect the shader via `ensureShinyShader()`.

### 3. `damageEntire()` does not require a live mapped window

Stock `CHyprBorderDecoration::damageEntire()`:

```cpp
if (!validMapped(m_window) ||
    Fullscreen::controller()->getFullscreenModes(m_window.lock()).internal == FSMODE_FULLSCREEN)
    return;
```

This plugin:

```cpp
// src/deco.cpp
void CShinyBorder::damageEntire() {
    CBox dm = assignedBoxGlobal();
    ...
}
```

```cpp
// src/main.cpp — window.active listener
for (auto& w : Desktop::windowState()->windows()) {
    if (auto* d = shinyOn(w))
        d->damageEntire();
}
```

`assignedBoxGlobal()` calls `getEdgeDefinedPoint` **before** locking the
window, then `pWindow->getWindowMainSurfaceBox()`. The focus listener walks
every window, including unmapped / closing ones. `g_pHyprRenderer` is also
unchecked here.

`onMouseMove` / `render.pre` do check `validMapped`. Focus does not. Unmap +
focus change is a normal path.

**Fix:** `if (!validMapped(m_window) || !g_pHyprRenderer) return;` at the top
of `damageEntire()`, matching stock. Guard `assignedBoxGlobal()` the same
way. Skip exclusive fullscreen if you are not drawing it (`renderWindow`
already sets `decorate = false` for `FSMODE_FULLSCREEN`).

### 4. `glBindVertexArray` on a possibly invalid id

```cpp
// src/pass.cpp
glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
```

Hyprland does stash the VAO in `m_uniformLocations[SHADER_SHADER_VAO]`, so
the lookup is not a category error. `createVao()` can still leave that slot
at `-1`. `glBindVertexArray(0xFFFFFFFF)` is `GL_INVALID_OPERATION`; on this
NVIDIA stack that has already been a compositor kill.

**Fix:**

```cpp
const GLint vao = shader->getUniformLocation(SHADER_SHADER_VAO);
if (vao <= 0)
    return {};
glBindVertexArray(vao);
```

---

## High — unload / attach footguns

### 5. `ensureShinyShader()` can recreate the program after `PLUGIN_EXIT`

`CShinyPassElement::draw()` calls `ensureShinyShader()` again. If a nested
leftover element is still in the pass (issue 1) and `draw` runs after
`destroyShinyShader()`, you compile a new program, then `dlclose`, then the
next clear still blows up — now with a shader allocated in a dying `.so` as
well.

`ensureShinyShader()` must refuse once teardown has started. A
`static bool g_tearingDown` set at the top of `PLUGIN_EXIT` is the cheap
guard, not a substitute for actually destroying the pass elements.

### 6. `pluginctl` can load two copies of the plugin

Hyprland `getPluginByPath` only rejects the same path. `pluginctl` copies to
`/tmp/hypr-shiny-border-$$.so` every load. Two loads without unload = two
`.so`s, two listener sets, two `g_shinyShader`s.

`windowHasShiny` is a string compare. `shinyOn` then
`dynamic_cast<CShinyBorder*>`. A decoration from copy A is not
`CShinyBorder` in copy B's RTTI, so B's `dynamic_cast` returns null. Unload
of A strips decorations Hyprland registered to A while B's listeners keep
running. Unload of B runs `removeAllOfType("CShinyPassElement")` and can pull
A's in-flight elements.

**Fix:** refuse load if `hyprctl plugin list` already contains
`hypr-shiny-border`, regardless of path. Keep the `/tmp` copy for `dlopen`
freshness; do not treat a new filename as a new plugin.

### 7. Function-local `CConfigValue` is a process-global raw pointer

```cpp
// src/deco.cpp CShinyBorder::borderSize()
static auto PBORDERSIZE = CConfigValue<Config::INTEGER>("general:border_size");
```

`CConfigValueBase` ctor pushes `this` into a compositor-global `registry()`.
`flushCaches()` walks that vector on every config reload. Official plugins
do this, and `~CConfigValueBase` does `std::erase`. It still means a
plugin-owned object is in Hyprland's global list until the function-local
static dies during `dlclose`.

If anything reloads config between `dlclose` starting and that destructor
running, `flushCaches()` is a use-after-unmap.

Safer: a member `CConfigValue` on `CShinyBorder` (dies with the deco, which
Hyprland strips before `dlclose`), or bind in `PLUGIN_INIT` and reset in
`PLUGIN_EXIT`.

---

## Correctness (not CTD)

### 8. Border thickness is scaled twice

`CShinyBorder::draw` stores `BS_PX = round(BORDERSIZE * pMonitor->m_scale)`
into `SData.borderSize`. `CShinyPassElement::draw` then does:

```cpp
shader->setUniformFloat(SHADER_THICK,
    sc<float>(m_data.borderSize) * sc<float>(mon->m_scale));
```

Stock `CHyprBorderDecoration` passes unscaled `borderSize` into
`CBorderPassElement` and lets the GL path scale. On a 2x monitor a 3px
border becomes 12. The SDF ring will eat into the client or halo too far.

Pass either logical px **or** already-scaled px, not both.

The fallback `CBorderPassElement` path still passes unscaled `BORDERSIZE`.
That split is how the double-scale survived.

### 9. Nest config key is the one the README says does nothing

README: Lua rewrites `-` to `_`; `["shiny-border"]` is an unknown key.

`nest/hyprland.lua` uses `["shiny-border"]`. The nest always runs on C++
defaults. `angle_offset` / `border_size` from that table never apply.

Use `shiny_border`.

### 10. CPU angle is computed, then usually ignored

`onMouseMove` quantizes `atan2` into `m_angle` to decide damage. The fragment
shader then does:

```glsl
float heading = angle;
if (dot(pointer_position, pointer_position) > 0.5) {
    vec2 dir = pointer_position - center;
    heading  = atan(-dir.y, dir.x);
}
```

Damage is quantized; the visible head is not. Pointer at transformed origin
(`dot <= 0.5`) silently falls back to the CPU angle.

Pick one source of heading. If the GPU pointer is the source of truth,
quantize that same heading on the CPU for damage, and drop the origin
special case.

---

## Structure

### 11. `windowHasShiny` / `shinyOn` should be one walk

`attach` only needs “already present?”. Everyone else needs the pointer.
`if (shinyOn(w)) return;` is enough. The string `"Shiny Border"` as identity
is also weak (issue 6). A deco member or `dynamic_cast` first is the actual
identity.

### 12. `CShinyBorder::draw` is two backends glued together

Geometry, colors, rounding, and pass submission are copy-pasted between the
custom path and `CBorderPassElement`. Pull box / rounding / colors into one
`SDrawParams`, then:

- shader ok → `CShinyPassElement`
- else → `CBorderPassElement`

Do not rebuild `CHyprColor{sc<uint64_t>(g_cfg.colA->value())}` twice.

### 13. Pulse should not scan every window on `render.pre`

`render.pre` is emitted only after Hyprland already decided this monitor
needs a frame, so this will not spin a VFR loop by itself. It also means
pulse dies when nothing else is damaging. You then damage every matching
window every such frame, including a pixman hole subtract.

Hyprland already has animated variables. A single `CAnimatedVariable<float>`
(or a 1/pulseHz timer) on the deco, damaged from that, deletes the “walk all
windows in pre-render” branch. `active_only` then falls out of “only the
focused deco has a running pulse,” not another filter in the listener.

### 14. Dead state on the deco

`m_lastPos` / `m_lastSize` are written in the ctor and `updateWindow` and
never read. `updateWindow` also `damageEntire()` on every call even when
size/border did not change. Stock only repositions on border-size change.

Drop the unused members; damage when geometry or `borderSize()` actually
changed.

---

## What is already right

- Hash mismatch throws; Hyprland catches it and ejects without `PLUGIN_EXIT`.
- Event-bus listeners reset in `PLUGIN_EXIT` before `dlclose`. Hyprland will
  not drop those SPs for you.
- `-fno-gnu-unique` is required for unload.
- `CShinyPassElement` copies `SData` by value (no `PHLWINDOW` on the shader
  path). That is the correct lifetime vs. window close mid-frame.
- Fallback `CBorderPassElement` is a compositor type; leaving it in the
  leftover pass is not a vtable bomb. Clearing the leftover pass (issue 1)
  still destroys it a frame early, which is fine.
- `pluginctl` refusing instance 0 is the correct default.

---

## Minimum before unload is safe

1. Destroy the leftover render pass completely in `PLUGIN_EXIT` (or otherwise
   destroy nested `CShinyPassElement`s) **before** `destroyShinyShader()` and
   **before** `dlclose`.
2. Never `glDelete*` without a current EGL context.
3. `damageEntire()` / `assignedBoxGlobal()` require `validMapped`.
4. Guard the VAO id.

Until those four are done, treat unload and `mise run reload` as “maybe the
nest dies,” not as a supported inner loop.

---

## Implementation phases

Sequential work packages, grouped by shared object graph / failure mode:
[phases.md](phases.md).

| Phase | Issues | Doc |
|---|---|---|
| 1 Unload teardown | 1, 2, 5 | [phase-1-unload-teardown.md](phase-1-unload-teardown.md) |
| 2 Runtime CTD | 3, 4 | [phase-2-runtime-ctd.md](phase-2-runtime-ctd.md) |
| 3 Reload footguns | 6, 7 | [phase-3-reload-footguns.md](phase-3-reload-footguns.md) |
| 4 Visual correctness | 8, 9, 10 | [phase-4-visual-correctness.md](phase-4-visual-correctness.md) |
| 5 Deco hygiene | 11, 14 | [phase-5-deco-hygiene.md](phase-5-deco-hygiene.md) |
| 6 Unify draw | 12 | [phase-6-unify-draw.md](phase-6-unify-draw.md) |
| 7 Pulse animation | 13 | [phase-7-pulse-animation.md](phase-7-pulse-animation.md) |

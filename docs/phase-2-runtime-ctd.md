# Phase 2 — Runtime CTD (mapped damage, VAO)

Status: done
Depends on: [phase 1](phase-1-unload-teardown.md)
Unlocks: the four CTD blockers in the review are done. Reload still waits on phase 3.
Review issues: **3**, **4**
Source: [code-review.md](code-review.md)

Independent crash paths, same bar: the plugin must not kill the compositor
while it is loaded. The review’s “minimum before unload is safe” list is
issues 1–4; 1/2 landed in phase 1, 3/4 land here.

## Why these together

Neither depends on the other. Both are remaining session-killing paths that
fire during normal use (unmap + focus, shader draw), not only on unload.
Land them before treating the four blockers as closed.

## Files

- `src/deco.cpp` — `damageEntire`, `assignedBoxGlobal`
- `src/deco.hpp` — only if signatures change (they should not)
- `src/pass.cpp` — `CShinyPassElement::draw` VAO bind
- `src/main.cpp` — no change required if `damageEntire` is the guard; the
  `window.active` listener already walks every window including unmapped ones
- `src/runtime.cpp` / `src/runtime.hpp` — compositor-free mapped/renderer/fullscreen and VAO decisions
- `tests/test_runtime.cpp`

## Work

### Issue 3 — `damageEntire()` / `assignedBoxGlobal()` require a live mapped window

Stock `CHyprBorderDecoration::damageEntire()`:

```cpp
if (!validMapped(m_window) ||
    Fullscreen::controller()->getFullscreenModes(m_window.lock()).internal == FSMODE_FULLSCREEN)
    return;
```

This plugin currently:

```cpp
void CShinyBorder::damageEntire() {
    CBox dm = assignedBoxGlobal();
    ...
}
```

`assignedBoxGlobal()` calls `getEdgeDefinedPoint` **before** locking the
window, then `pWindow->getWindowMainSurfaceBox()`. The focus listener walks
every window, including unmapped / closing ones. `g_pHyprRenderer` is also
unchecked. `onMouseMove` / `render.pre` check `validMapped`; focus does not.
Unmap + focus change is a normal path.

Fix:

```cpp
void CShinyBorder::damageEntire() {
    if (!validMapped(m_window) || !g_pHyprRenderer)
        return;
    // skip exclusive fullscreen — renderWindow already sets decorate = false
    // for FSMODE_FULLSCREEN
    ...
}
```

Guard `assignedBoxGlobal()` the same way (`validMapped` / lock before
`getEdgeDefinedPoint`). Match stock’s fullscreen early-out if this deco is
not drawing exclusive fullscreen.

Do **not** paper this over by filtering the focus listener only. Other callers
(`updateWindow`, mouse move, later pulse) must hit the same guard.

### Issue 4 — `glBindVertexArray` on a possibly invalid id

```cpp
glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
```

Hyprland does stash the VAO in `m_uniformLocations[SHADER_SHADER_VAO]`, so the
lookup is not a category error. `createVao()` can still leave that slot at
`-1`. `glBindVertexArray(0xFFFFFFFF)` is `GL_INVALID_OPERATION`; on this
NVIDIA stack that has already been a compositor kill.

```cpp
const GLint vao = shader->getUniformLocation(SHADER_SHADER_VAO);
if (vao <= 0)
    return {};
glBindVertexArray(vao);
```

## Out of scope

- Pulse still scans every window on `render.pre` (issue 13, phase 7). That
  path becomes safe because `damageEntire` now returns on unmapped windows.
- Heading / scale (issues 8, 10) — phase 4.

## Done when

- `damageEntire` and `assignedBoxGlobal` no-op without `validMapped` and
  without `g_pHyprRenderer`.
- Exclusive fullscreen is skipped if we do not draw it.
- Shader `draw()` does not bind VAO id `<= 0`.
- Focus change during unmap/close does not CTD.

## Verify

In the nest:

- Close the focused window; focus moves. Nest stays up.
- Unmap / workspace change with `active_only` on and off.
- Exclusive fullscreen a client; no damage crash, no decorate-over-fullscreen.
- Shader path draws (VAO > 0). If you can force a failed `createVao`, draw
  returns rather than binding `-1`.

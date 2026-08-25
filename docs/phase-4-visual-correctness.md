# Phase 4 — Visual correctness (scale, nest key, heading)

Status: not started
Depends on: [phase 3](phase-3-reload-footguns.md) (reload is now the way you iterate)
Unlocks: phases 5–7 can refactor without chasing “why is the ring 4× thick”
Review issues: **8**, **9**, **10**
Source: [code-review.md](code-review.md)

Not CTD. The border is the wrong thickness, nest config is ignored, and the
CPU angle is not what the fragment shader draws. Fix the look before
restructuring the draw path (phase 6) or pulse (phase 7).

## Why these together

All three are “the nest does not show what the README describes.” Issue 9
must land with 8: the nest currently cannot apply `border_size`, so you
cannot verify thickness from nest config. Issue 10 is independent code but
the same visual QA pass (cursor-facing head on a 1× and 2× monitor).

Do **not** fold in issue 12 (unify the two backends). That is a refactor.
This phase is a surgical scale contract those backends must keep.

## Files

- `src/deco.cpp` — `CShinyBorder::draw` stores `BS_PX` already scaled
- `src/pass.cpp` — `SHADER_THICK` multiplies `m_data.borderSize` by
  `mon->m_scale` again
- `src/pass.hpp` — comment on `SData.borderSize` (logical vs scaled)
- `src/main.cpp` — `onMouseMove` CPU `atan2` / `quantize`
- `src/shaders.hpp` — fragment heading + `dot(pointer, pointer) > 0.5`
- `nest/hyprland.lua` — `["shiny-border"]` → `shiny_border`

## Work

### Issue 9 — nest config key (do this first)

README: Lua rewrites `-` to `_`; `["shiny-border"]` is an unknown key.
`nest/hyprland.lua` uses `["shiny-border"]`. The nest always runs on C++
defaults. `angle_offset` / `border_size` from that table never apply.

Use `shiny_border` (unquoted identifier is fine in Lua), matching the README
example. No C++ key change — `plugin:shiny-border:…` stays.

### Issue 8 — border thickness is scaled twice

`CShinyBorder::draw` stores `BS_PX = round(BORDERSIZE * pMonitor->m_scale)`
into `SData.borderSize`. `CShinyPassElement::draw` then does:

```cpp
shader->setUniformFloat(SHADER_THICK,
    sc<float>(m_data.borderSize) * sc<float>(mon->m_scale));
```

Stock `CHyprBorderDecoration` passes **unscaled** `borderSize` into
`CBorderPassElement` and lets the GL path scale. On a 2× monitor a 3px
border becomes 12. The SDF ring eats into the client or halo too far.

Pick **one** contract and write it on `SData.borderSize`:

- logical px, shader multiplies by `mon->m_scale`, **or**
- already-scaled px, shader does not multiply

Match stock: pass unscaled `BORDERSIZE` (same as the fallback
`CBorderPassElement` path, which already passes unscaled). That is how the
double-scale survived — the two backends disagreed.

Rounding (`ROUNDING`, `OUTERROUND`) is already scaled in `draw()` and passed
straight through. Do not “fix” those unless you find they are also doubled.
This issue is `SHADER_THICK` / `borderSize` only.

Phase 6 will share one params struct. Document the contract so that refactor
cannot reintroduce the extra multiply.

### Issue 10 — CPU angle is computed, then usually ignored

`onMouseMove` quantizes `atan2` into `m_angle` to decide damage. The
fragment shader then does:

```glsl
float heading = angle;
if (dot(pointer_position, pointer_position) > 0.5) {
    vec2 dir = pointer_position - center;
    heading  = atan(-dir.y, dir.x);
}
```

Damage is quantized; the visible head is not. Pointer at transformed origin
(`dot <= 0.5`) silently falls back to the CPU angle.

**Pick one source of heading: the GPU pointer is the source of truth.**

- Shader: always `heading = atan(-dir.y, dir.x)` from `pointer_position` vs
  box center. Drop the origin special case.
- CPU: compute **that same heading** (same space as the pointer passed in
  `SData.pointer`), then `quantize` it for the damage decision. `m_angle`
  remains the quantized damage latch, not a second visible heading.
- If the uniform `angle` is unused after this, stop uploading it (or keep it
  only as a debug fallback — default is drop).

`angle_offset` / `quantize_deg` still apply on the CPU damage path so
chatter does not change. They must not fight a different GPU heading.

## Out of scope

- Unifying shader vs `CBorderPassElement` setup (issue 12) — phase 6.
- Pulse scan (issue 13) — phase 7. Pulse still works; it is just expensive.

## Done when

- Nest `plugin.shiny_border.*` actually applies (`hyprctl getoption` /
  visible `border_size`, `angle_offset`).
- On scale=1 and scale=2, a configured 3px ring is 3 framebuffer pixels ×
  scale, not × scale².
- Fallback `CBorderPassElement` thickness still matches the shader path
  (both unscaled in, GL scales once).
- Highlight head tracks the cursor; moving onto the transformed origin does
  not snap to a stale CPU angle. Damage still quantizes.

## Verify

In the nest, after this phase’s reload:

- Change `border_size` / `angle_offset` in `nest/hyprland.lua`, reload
  config. Values take effect.
- 1× and (if available) 2× monitor: ring thickness vs a known `border_size`.
- Kill the shader (break the frag, reload) and confirm fallback thickness
  matches.
- Move the mouse around a window, including across its center. Head follows
  continuously; damage does not fire every pixel (quantize still holds).

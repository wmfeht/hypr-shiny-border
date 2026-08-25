# Phase 7 — Pulse on the deco, not `render.pre`

Status: done
Depends on: [phase 2](phase-2-runtime-ctd.md) (`damageEntire` is mapped-safe),
[phase 5](phase-5-deco-hygiene.md) (`updateWindow` is not already damaging every time),
[phase 6](phase-6-unify-draw.md) (`draw()` is one params path)
Unlocks: nothing further from this review
Review issues: **13**
Source: [code-review.md](code-review.md)

Last because it replaces a global listener with per-deco animation state.
It needs a `damageEntire` that will not CTD on unmapped windows, and it
should not land in the same diff as the draw-backend merge.

## Why this is last

`render.pre` is emitted only after Hyprland already decided this monitor
needs a frame, so this will not spin a VFR loop by itself. It also means
pulse **dies when nothing else is damaging**. You then damage every matching
window every such frame, including a pixman hole subtract.

That is a behavior change (pulse can keep a window damaging on its own) plus
a structure change (delete the all-windows scan). Do it when identity,
damage, and `draw()` are already settled so `active_only` becomes “only the
focused deco has a running pulse,” not another filter in the listener.

## Files

- `src/main.cpp` — delete `g_onTick` / `render.pre` listener
- `src/globals.hpp` — drop `g_onTick`
- `src/deco.cpp` / `src/deco.hpp` — animated pulse (or 1/`pulseHz` timer),
  damage from that
- `src/pass.cpp` / `shaders.hpp` — only if time/pulse uniforms start coming
  from the animated value instead of `m_globalTimer`

## Work

Hyprland already has animated variables. A single
`CAnimatedVariable<float>` (or a 1/`pulseHz` timer) on the deco, damaged
from that, deletes the “walk all windows in pre-render” branch.

`active_only` then falls out of “only the focused deco has a running pulse,”
not another filter in the listener. When focus leaves, stop the animation
(or let it idle at a rest value) and damage once. When focus arrives, start
it. The existing `window.active` listener can be the place that starts/stops
rather than damaging every window blindly — but it must still go through
phase 2’s `damageEntire` guard.

Constraints:

- Do not reintroduce a full-window-list scan on `render.pre`.
- Pulse still dies when `plugin:shiny-border:pulse` is false; `draw()`
  already passes `time = 0` / `pulseHz = 0` in that case — keep that.
- `active_only` false: every mapped shiny deco may run a pulse. That is N
  animations, not N scans of the window list per frame from a global
  listener.
- VFR: the animation’s own damage is what schedules frames. That is
  intended (the ring breathes while idle). Do not also damage from
  `render.pre`.
- Unmap / close: animation callback must not `damageEntire` without
  `validMapped` (phase 2). Reset the listener in the deco destructor if it
  is not already tied to the deco lifetime.

If `CAnimatedVariable` is the wrong shape for “loop a sine at `pulseHz`”
on this Hyprland version, a per-deco timer that calls `damageEntire` at the
pulse rate is the alternative named in the review. Do not add a process-wide
tick back.

`draw()` can keep sampling `g_pHyprRenderer->m_globalTimer` for the sine
phase (continuous time) even if damage is timer-driven — or drive both from
the animated value. Pick one clock so the ring does not hitch when damage
rate and shader time disagree.

## Out of scope

- New pulse look (waveform, color). This is how pulse is scheduled.
- Re-opening heading/scale.

## Done when

- No `render.pre` / `g_onTick` window walk.
- Pulse continues while the nest is otherwise idle (animation damages).
- `active_only` on: only the focused window breathes. Off: all mapped shiny
  windows breathe without a global scan.
- Unmap / close / unload: no timer/animation callback into a dead deco.

## Verify

In the nest:

- Pulse on, sit idle: ring still breathes (this is the behavior change vs
  “pulse dies when nothing else damages”).
- Pulse off: static ring, no extra frames from this plugin.
- `active_only` on: unfocused windows static; focused window pulses.
- `active_only` off: all visible shiny windows pulse.
- Close the pulsing window, unload the plugin: nest stays up.
- `mise run reload` with pulse on: still the supported inner loop from
  phase 3.

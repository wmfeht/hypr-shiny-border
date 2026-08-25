# Phase 5 — Decoration hygiene (one walk, no dead state)

Status: not started
Depends on: [phase 4](phase-4-visual-correctness.md) (behavior is correct; this is structure)
Unlocks: [phase 7](phase-7-pulse-animation.md) (pulse damage needs a dirty-check that does not always `damageEntire`)
Review issues: **11**, **14**
Source: [code-review.md](code-review.md)

Local cleanup on `CShinyBorder` and the attach helpers. No visual change.
Do this before the draw-backend refactor (phase 6) and the pulse rewrite
(phase 7) so those diffs are not also deleting dead members and dual walks.

## Why these together

Both are “the deco object graph lies.” `windowHasShiny` / `shinyOn` walk the
same list twice with a string identity that issue 6 already proved is weak.
`m_lastPos` / `m_lastSize` pretend to track geometry and never do.
`updateWindow` damages on every call. Phase 7’s per-deco pulse is nonsense
if every `updateWindow` already damages the world.

## Files

- `src/main.cpp` — `windowHasShiny`, `shinyOn`, `attach`, listeners that
  only needed a boolean
- `src/deco.cpp` — `updateWindow`, ctor
- `src/deco.hpp` — drop or actually use last-geometry members

## Work

### Issue 11 — `windowHasShiny` / `shinyOn` should be one walk

`attach` only needs “already present?”. Everyone else needs the pointer.
`if (shinyOn(w)) return;` is enough.

The string `"Shiny Border"` as identity is also weak (issue 6: a second
`.so` has a different `CShinyBorder` RTTI and the same display name).
**Identity is `dynamic_cast<CShinyBorder*>` first**, not
`getDisplayName() == "Shiny Border"`. Keep `getDisplayName()` for Hyprland
UI; do not use it as the attach/lookup key.

One helper:

```cpp
static CShinyBorder* shinyOn(PHLWINDOW window) {
    for (auto& d : window->m_windowDecorations) {
        if (auto* shiny = dynamic_cast<CShinyBorder*>(d.get()))
            return shiny;
    }
    return nullptr;
}
```

`attach`: `if (!validMapped(window) || shinyOn(window)) return;`

Delete `windowHasShiny`.

### Issue 14 — dead state on the deco

`m_lastPos` / `m_lastSize` are written in the ctor and `updateWindow` and
never read. `updateWindow` also `damageEntire()` on every call even when
size/border did not change. Stock only repositions on border-size change.

`m_lastSizeB` is already the border-size dirty flag for `repositionDeco`.
Keep that.

For geometry: either **read** last pos/size and only damage on change, or
**drop** them and compare something you already have (`m_assignedGeometry`,
window pos/size). They must not stay write-only.

```cpp
void CShinyBorder::updateWindow(PHLWINDOW pWindow) {
    const auto pos  = pWindow->position(IGeometric::GEOMETRIC_CURRENT);
    const auto size = pWindow->size(IGeometric::GEOMETRIC_CURRENT);
    const int  bs   = borderSize();

    const bool borderChanged = (bs != m_lastSizeB);
    const bool geoChanged    = (pos != m_lastPos || size != m_lastSize);

    if (borderChanged) {
        m_lastSizeB = bs;
        g_pDecorationPositioner->repositionDeco(this);
    }

    m_lastPos  = pos;
    m_lastSize = size;

    if (geoChanged || borderChanged)
        damageEntire();
}
```

If you can dirty-check without storing pos/size, delete the members. If you
keep them, they exist to implement the dirty check — that is the whole point.

`damageEntire` still has phase 2’s `validMapped` guard.

## Out of scope

- `CShinyBorder::draw` two-backend split (issue 12) — phase 6.
- Replacing `render.pre` pulse (issue 13) — phase 7. After this phase,
  `updateWindow` will not hide pulse bugs by damaging every time anyway.

## Done when

- One decoration walk, `dynamic_cast` identity, no `windowHasShiny`.
- `updateWindow` repositions only on border-size change and damages only on
  geometry or border-size change.
- No write-only pos/size members.

## Verify

In the nest:

- Open / map windows: still one shiny deco per window, not two.
- Resize and move: ring follows, no extra flicker vs current.
- Change `plugin:shiny-border:border_size` (or `general:border_size` when
  plugin size is `-1`): deco repositions and damages.
- Unchanged windows during an unrelated config reload: no mandatory ring
  damage from `updateWindow` unless geometry/border actually changed.

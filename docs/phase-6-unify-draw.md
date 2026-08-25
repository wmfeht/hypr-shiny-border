# Phase 6 — Unify draw backends

Status: not started
Depends on: [phase 4](phase-4-visual-correctness.md) (scale contract), [phase 5](phase-5-deco-hygiene.md) (deco is not also being gutted)
Unlocks: [phase 7](phase-7-pulse-animation.md) can change *when* we draw without fighting a duplicated `draw()`
Review issues: **12**
Source: [code-review.md](code-review.md)

`CShinyBorder::draw` is two backends glued together. Geometry, colors,
rounding, and pass submission are copy-pasted. Phase 4’s double-scale bug
survived **because** the fallback path passed unscaled `BORDERSIZE` and the
shader path did not. One params struct makes that split impossible to
reintroduce by accident.

## Why this is its own phase

Issue 12 is a refactor of the hottest function in the plugin. Mixing it with
heading (phase 4) or pulse (phase 7) hides regressions. It is a single
issue, but it is the structure item that must preserve issue 8’s contract.

## Files

- `src/deco.cpp` — `CShinyBorder::draw`
- `src/deco.hpp` — private `SDrawParams` (or equivalent) if it is not local
  to the `.cpp`
- `src/pass.hpp` — `CShinyPassElement::SData` should consume the shared
  fields, not grow a second copy

## Work

Pull box / rounding / colors into one `SDrawParams`, then:

- shader ok → `CShinyPassElement`
- else → `CBorderPassElement`

Do not rebuild `CHyprColor{sc<uint64_t>(g_cfg.colA->value())}` twice.

Sketch:

```cpp
struct SDrawParams {
    CBox       outerBox;
    CHyprColor colA;
    CHyprColor colB;
    int        rounding;
    int        outerRound;
    float      roundingPower;
    int        borderSize; // LOGICAL px — GL scales once (phase 4)
    float      a;
    // shader-only: pointer, angle/time/pulse/lobe as today
};

void CShinyBorder::draw(PHLMONITOR pMonitor, float const& a) {
    // existing early-outs (validMapped, enabled, active_only, size, geometry)
    const SDrawParams p = buildDrawParams(pMonitor, a); // one place
    if (ensureShinyShader()) {
        g_pHyprRenderer->addPassElement(makeUnique<CShinyPassElement>(toShinyData(p)));
        return;
    }
    g_pHyprRenderer->addPassElement(makeUnique<CBorderPassElement>(toBorderData(p)));
}
```

Names are local taste. The rule is: **one** computation of box, rounding,
colors, logical `borderSize`. Submission is two lines.

Preserve phase 4:

- `SData.borderSize` / `SBorderData.borderSize` stay **unscaled** (or
  whatever contract phase 4 documented). Shader `SHADER_THICK` scales at
  most once.
- `ensureShinyShader()` still respects `g_tearingDown` (phase 1).
- Fallback `CBorderPassElement` remains a compositor type; do not start
  putting `PHLWINDOW` on the shader `SData` (the review calls the by-value
  copy correct vs window close mid-frame).

Do not “simplify” by dropping the fallback. Shader compile failure is still
a supported path.

## Out of scope

- Pulse listener → `CAnimatedVariable` (issue 13) — phase 7.
- Changing SDF / comet look.

## Done when

- Geometry, rounding, colors, logical thickness exist in one place.
- Shader path and `CBorderPassElement` path cannot disagree on those.
- Phase 4 scale and heading contracts still hold.
- No `PHLWINDOW` on `CShinyPassElement::SData`.

## Verify

In the nest:

- Shader path: same look as end of phase 4 (thickness, heading, pulse).
- Break the fragment shader, reload: fallback ring still draws at the same
  thickness/rounding/colors (linear gradient, not comet — that difference is
  intended).
- Close a window mid-frame: no use-after-free on the shader path (SData
  still by value).

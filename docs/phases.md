# Implementation phases

Source: [code-review.md](code-review.md) (full tree at `608ea92`).

Do these in order. Issues that share an object graph, a function, or a
failure mode are in the same phase. Later phases assume earlier ones stuck.

**Do not treat `mise run reload` as a supported inner loop until phases 1–3
are done.** Login-session load of this `.so` stays a known risk after that;
iterate in the nest.

| Phase | Document | Review issues | Gate |
|---|---|---|---|
| 1 | [phase-1-unload-teardown.md](phase-1-unload-teardown.md) | 1, 2, 5 | Leftover pass and shader cannot CTD on `PLUGIN_EXIT` |
| 2 | [phase-2-runtime-ctd.md](phase-2-runtime-ctd.md) | 3, 4 | Remaining CTD blockers. Unload is no longer “maybe the nest dies” for the four listed crash paths |
| 3 | [phase-3-reload-footguns.md](phase-3-reload-footguns.md) | 6, 7 | Duplicate `.so` and `CConfigValue` lifetime. Reload is a supported inner loop |
| 4 | [phase-4-visual-correctness.md](phase-4-visual-correctness.md) | 8, 9, 10 | Thickness, nest config, heading match what the README claims |
| 5 | [phase-5-deco-hygiene.md](phase-5-deco-hygiene.md) | 11, 14 | One identity walk; damage only when geometry actually changed |
| 6 | [phase-6-unify-draw.md](phase-6-unify-draw.md) | 12 | One `SDrawParams`; shader vs stock fallback share geometry/colors |
| 7 | [phase-7-pulse-animation.md](phase-7-pulse-animation.md) | 13 | Pulse lives on the deco, not a `render.pre` window scan |

```
1 (unload pass/shader) ─┐
2 (damage + VAO)        ├─► 3 (reload footguns) ─► 4 (visual)
                        │                              │
                        └──────────────────────────────┼─► 5 (deco hygiene)
                                                       │         │
                                                       └─────────┼─► 6 (unify draw)
                                                                 │
                                                       5 ────────┴─► 7 (pulse)
```

Phase 6 must not reintroduce the double-scale from issue 8. Phase 7 needs
phase 2’s `validMapped` guard and phase 5’s “damage only when dirty.”

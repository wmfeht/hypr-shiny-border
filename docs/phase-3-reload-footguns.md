# Phase 3 — Reload footguns (one `.so`, no process-global `CConfigValue`)

Status: done
Depends on: [phase 1](phase-1-unload-teardown.md), [phase 2](phase-2-runtime-ctd.md)
Unlocks: `mise run reload` as a supported inner loop. Phase 4 visual work.
Review issues: **6**, **7**
Source: [code-review.md](code-review.md)

Both are “Hyprland still holds a plugin object after we think we are gone,”
just not the render-pass vtable. After this phase, unload/reload is the inner
loop the nest was built for.

## Why these together

The high-severity section of the review. Neither is a leftover-pass SIGSEGV,
but each makes `pluginctl reload` (copy to `/tmp/…-$$.so`, unload, load) a
second crash path. Fix them in one pass over the load/unload tooling and the
one compositor-global we still register from C++.

## Files

- `scripts/pluginctl.sh` — load must refuse a second copy by **name**
- `src/deco.cpp` — `CShinyBorder::borderSize()` function-local `CConfigValue`
- `src/globals.hpp` / `src/main.cpp` — if the `general:border_size` handle
  moves to plugin-owned storage created in `PLUGIN_INIT` and reset in
  `PLUGIN_EXIT`

## Work

### Issue 6 — `pluginctl` can load two copies

Hyprland `getPluginByPath` only rejects the same path. `pluginctl` copies to
`/tmp/hypr-shiny-border-$$.so` every load. Two loads without unload = two
`.so`s, two listener sets, two `g_shinyShader`s.

`windowHasShiny` is a string compare. `shinyOn` then
`dynamic_cast<CShinyBorder*>`. A decoration from copy A is not `CShinyBorder`
in copy B's RTTI, so B's `dynamic_cast` returns null. Unload of A strips
decorations Hyprland registered to A while B's listeners keep running. Unload
of B runs pass cleanup and can pull A's in-flight elements.

**Fix:** before `plugin load`, refuse if `hyprctl -i "$target" plugin list`
already contains `hypr-shiny-border`, **regardless of path**. Keep the `/tmp`
copy for `dlopen` freshness; do not treat a new filename as a new plugin.

`reload` stays `unload` then `load`. If unload failed and the name is still
listed, load must still refuse rather than stacking a second copy.

Do not weaken `SHINY_INSTANCE=0` / `SHINY_LIVE=1`. Instance 0 stays refused
by default.

### Issue 7 — function-local `CConfigValue` is a process-global raw pointer

```cpp
static auto PBORDERSIZE = CConfigValue<Config::INTEGER>("general:border_size");
```

`CConfigValueBase` ctor pushes `this` into a compositor-global `registry()`.
`flushCaches()` walks that vector on every config reload. Official plugins do
this, and `~CConfigValueBase` does `std::erase`. It still means a
plugin-owned object is in Hyprland's global list until the function-local
static dies during `dlclose`. If anything reloads config between `dlclose`
starting and that destructor running, `flushCaches()` is a use-after-unmap.

**Fix (pick one, prefer the first):**

1. Bind once in `PLUGIN_INIT`, reset in `PLUGIN_EXIT` (plugin-owned
   `UP`/`SP`/`std::optional` on `g_cfg` or `globals.hpp`). One handle, dies
   on the `PLUGIN_EXIT` path we already control, before `dlclose`.
2. Member `CConfigValue` on `CShinyBorder` — dies with the deco, which
   Hyprland strips before `dlclose`. Worse: every window registers another
   copy of the same key.

Do **not** leave the function-local static. Do **not** skip `~CConfigValue`
on the exit path.

## Out of scope

- Collapsing `windowHasShiny` / `shinyOn` (issue 11) — phase 5. After this
  phase, string identity is still weak; the load-time name check is what
  stops two RTTI domains.
- README already documents Lua `shiny_border`; nest still uses the broken
  key (issue 9, phase 4).

## Done when

- `pluginctl load` while the plugin name is already listed exits non-zero and
  does not `plugin load` a second path.
- `pluginctl reload` is unload-then-load and never stacks copies.
- `general:border_size` is not a function-local `CConfigValue`. The handle
  is destroyed in `PLUGIN_EXIT` (or with the deco) before `dlclose`.

## Verify

In the nest:

- `mise run load` twice: second load refused, one entry in `plugin list`.
- `mise run reload` in a loop (a few cycles) with windows open, config
  reload (`ALT+R` / `hyprctl reload`) in between. Nest stays up.
- With `plugin:shiny-border:border_size = -1`, thickness follows
  `general:border_size` (this is the handle you just moved).

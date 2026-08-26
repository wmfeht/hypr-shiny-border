# Development

User install and config live in the [README](README.md).

A plugin crash is a compositor crash. Iterate in a nested Hyprland, not the
login session.

**Target:** Hyprland **v0.56.2**. Build with the same compiler that built the
compositor, against the headers in `/usr/include/hyprland`. `mise run headers`
must show the same git hash for `hyprctl version` and `version.h`.

## How it works

Hyprland already has gradient borders and `borderangle`. This plugin does
something else: a **conic comet on a rounded-rect ring**, aimed at the mouse,
relative to the window.

- `input.mouse.move` computes the heading from the window center to the cursor
- the angle is quantized, then only the ring is damaged if it changed
- `CShinyBorder` is a custom `IHyprWindowDecoration`, so the plugin owns the
  draw path instead of poking Hyprland’s border-color angle field
- a GLES 3 fragment shader draws the comet (`src/shaders.hpp`); if that
  program fails to compile, draw falls back to Hyprland’s linear
  `CBorderPassElement`
- optional pulse lives on the decoration (`CEventLoopTimer`), not a global
  `render.pre` scan
- optional shimmer reuses that timer, exclusive with pulse (shimmer wins):
  two CPU-side random walks (`shinyShimmerStep`, seeded per deco) modulate the
  heading and the highlight size, then the pass gets a final angle / lobe /
  thickness scale — the shader draws its nominal branch, no new uniforms
- optional pin (`pin` / `pin_deg`) replaces the mouse latch with a fixed
  heading; `onMouseMove` bails out early and `draw` computes the pinned angle
  live, so pulse/shimmer still animate around it

`active_only` (default on) means only the focused window tracks the cursor and
pulses. Unfocused windows have **no** ring, but they still reserve the same
padding so focus does not reflow the client. There is no inactive shiny
border. `enabled = false` reserves 0 px.

## Build

Needs Hyprland headers, `pkg-config`, and the compositor’s `g++` (`gnu++26`,
`-fno-gnu-unique`). Logic tests do not: they compile `runtime.cpp` /
`teardown.cpp` only.

`hyprpm.toml` runs `make all` with `PKG_CONFIG_PATH` pointing at hyprpm’s
header tree, not necessarily `/usr`.

```sh
mise run build     # make -j$(nproc) → hypr-shiny-border.so
mise run test      # compositor-free logic tests (no headers, no .so)
mise run test-full # + pluginctl reload harness (needs the .so / nest box)
mise run headers   # running compositor hash vs installed headers
```

## Nest loop

```text
mise run nest          # nested Hyprland, nest/hyprland.lua
                       # ALT+Return = foot, ALT+Q = close, ALT+M = kill nest

# outer terminal, nest already up:
mise run reload        # make + copy to a fresh /tmp path + unload/load
                       # last hyprctl instance; refuses the login session
```

`scripts/pluginctl.sh` copies the `.so` to a new `/tmp` path on every load so
`dlopen` cannot keep a stale mapping of the same filename. It refuses instance
0 (the login session) unless you set both `SHINY_INSTANCE=0` and `SHINY_LIVE=1`.

```
edit src/  →  mise run reload  →  wiggle the mouse  →  repeat
```

If the nest dies, close the window and `mise run nest` again. `PLUGIN_EXIT` is
not called on a fault.

To load into the live session (only with a `.so` you already trust):

```
SHINY_INSTANCE=0 SHINY_LIVE=1 mise run load
```

gdb: `gdb --args Hyprland --config $PWD/nest/hyprland.lua`.

Manual hyprctl against a nest:

```sh
hyprctl plugin load /absolute/path/to/hypr-shiny-border.so
hyprctl plugin unload /absolute/path/to/hypr-shiny-border.so
hyprctl plugin list
```

Same-path reload can reuse the mapping; `pluginctl` avoids that by copying.

## Layout

```
src/main.cpp     PLUGIN_INIT / EXIT, listeners, heading
src/deco.*       IHyprWindowDecoration, pulse timer
src/pass.*       CShinyPassElement, shader compile
src/teardown.*   teardown mark + shader lifecycle
src/runtime.*    mapped-window / VAO guards
src/shaders.hpp  vertex + conic comet fragment
tests/           compositor-free logic tests; mise run test-full adds pluginctl
nest/hyprland.lua
scripts/pluginctl.sh
```

## Notes

- **Same compiler as Hyprland.** Do not let mise hand you a different `g++`.
- **`-fno-gnu-unique`** (`--no-gnu-unique`) or unload does not actually unload.
- **Hash check** in `PLUGIN_INIT`. Skipping it is how plugins SIGSEGV after the
  next compositor update.
- **Lua key is `shiny_border`.** Hyphens become underscores.
- **`PLUGIN_EXIT`** must reset `Event::bus()` listeners, `m_renderPass.clear()`
  the leftover already-rendered pass, and destroy the shader *before*
  `dlclose`. Hyprland strips decorations; it does not flush plugin pass
  elements. Surgical `removeAllOfType("CShinyPassElement")` does not recurse
  into nested transformer passes. Do not `removeAllOfType("CBorderPassElement")`
  — that pass is also the stock border.
- Function hooks are a last resort. This plugin uses `Event::bus()`.

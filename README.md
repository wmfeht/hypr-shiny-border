# hypr-shiny-border

Window borders that look at the mouse.

Hyprland already does gradient borders. It already spins them with `borderangle loop` if you enjoy melting your battery. This plugin is the other trick: the bright stop of the gradient **faces the cursor**, relative to the window.

This is a Hyprland compositor plugin. It is **not** an Omarchy-shell plugin. The bar cannot draw window decorations. If you clone `omarchy.clock` you will get a clock.

Target: **Hyprland v0.56.2** (the one Omarchy has installed). Headers live in `/usr/include/hyprland`. If `mise run headers` shows two different git hashes, stop and rebuild.

Status: scaffold that should compile and load. The look is a linear two-stop gradient whose angle is `atan2(cursor - windowCenter)`. It is not a conic spotlight. That is a later, meaner shader.

## Why a plugin

| Approach | What you get | What you pay |
|---|---|---|
| Lua timer + `set_prop` | works, ~40 lines | polls at 16 ms, no pointer event in Lua |
| `borderangle loop` | rainbow vomit | full refresh-rate redraw even when idle |
| **This** | real `input.mouse.move`, damage only the ring | C++ against an unstable ABI |
| Pure Rust `.so` | a good tweet | you still write a C++ vtable for `IHyprWindowDecoration` |

Vaxry, on Rust: *"no ABI, and a slow ass compiler."* The Hyprland plugin API is C++ objects with C symbol names on purpose. A C++ shim + Rust `atan2` is how you annoy him without also failing `dlopen`. That shim is not in this tree yet. The `.so` is C++.

## How it is wired

```
mouse.move  ──►  atan2(cursor - window.middle())
                 quantize to N degrees
                 CShinyBorder::setAngle
                 damageEntire()            ── only if the angle changed

window.open ──►  addWindowDecoration(CShinyBorder)

draw()      ──►  CBorderPassElement with CGradientValueData(colA, colB, angle)
```

`CShinyBorder` is a custom `IHyprWindowDecoration`. That is the borders-plus-plus pattern: we own the draw path so we do not poke `PHLWINDOW->m_realBorderColor.m_angle`. That field already moved onto the decoration on git `main`. Touching it is how plugins die across one Hyprland bump.

Stock `borderangle` / `border` animations must stay **off** while this runs, or Hyprland interpolates the color and the gradient lags the mouse. The nest config disables both.

`active_only` (default on) means only the focused window tracks. Pointing every visible window at the cursor is N border damages per motion event. Cool, expensive, later.

## Config

Lua, after the plugin is loaded:

```lua
hl.config({
  general = { border_size = 0 },   -- stock ring off; we draw ours
  plugin = {
    ["shiny-border"] = {
      enabled      = true,
      active_only  = true,
      quantize_deg = 1,    -- 5 is cheaper if it chatters
      angle_offset = 0,    -- add 90 if the wrong stop faces the mouse
      border_size  = 3,    -- -1 = follow general:border_size
      -- col.a / col.b are ARGB ints; set via hyprctl if the table form is fussy
    },
  },
})
```

Keys in C++ are `plugin:shiny-border:…`. Defaults for the two stops match Omarchy's `rgba(33ccffee)` / `rgba(00ff99ee)`.

If you leave `general:border_size` at 2 you will get **two** rings: Hyprland's plus ours. The nest zeros the stock one.

## Dev cycle

Do **not** iterate this on the live Omarchy session. A plugin crash is a compositor crash.

```text
mise run nest          # nested Hyprland in a window, nest/hyprland.lua
                       # ALT+Return = foot, ALT+Q = close, ALT+M = kill nest

# outer terminal:
mise run headers       # hashes must match
mise run reload        # make + copy to /tmp + unload/load into the *last* instance
```

`scripts/pluginctl.sh` copies the `.so` to a new `/tmp` path on every load so `dlopen` cannot keep a stale mapping of the same filename. It **refuses instance 0** (the live Omarchy session). Last time that path SIGSEGV'd Hyprland at `CRenderPass::clear()` — leftover `CShinyPassElement` objects, `.so` already unmapped. `SHINY_INSTANCE=0 SHINY_LIVE=1 mise run load` is the override, and a bad idea.

Inner loop once the nest is up:

```
edit src/  →  mise run reload  →  wiggle the mouse  →  repeat
```

If the nest dies: close the window, `mise run nest` again. `PLUGIN_EXIT` is **not** called on a fault.

gdb: `gdb --args Hyprland --config $PWD/nest/hyprland.lua`, then `bt` when it explodes.

## Layout

```
src/main.cpp     PLUGIN_INIT / EXIT, listeners, atan2
src/deco.*       IHyprWindowDecoration
src/globals.hpp  HANDLE, config SPs, signal listeners (must stay alive)
nest/hyprland.lua
scripts/pluginctl.sh
hyprpm.toml      for later, not for this loop
mise.toml        tasks
```

`hyprpm` is how other people install this. It is not the inner loop. `make` + `hyprctl plugin load` is.

## Landmines

- **Same compiler as Hyprland.** GCC 16.2.1 on this box. mise is not allowed to hand you a different g++.
- **`--no-gnu-unique`** (`-fno-gnu-unique`) or unload is a lie.
- **Hash check** in `PLUGIN_INIT`. If you skip it you get a "fun" SIGSEGV after the next `omarchy update`.
- **Do not** `removeAllOfType("CBorderPassElement")` on exit. That pass is also the stock border.
- **Do** `removeAllOfType("CShinyPassElement")` and reset `Event::bus()` listeners in `PLUGIN_EXIT`. Hyprland strips decorations; it does **not** flush plugin pass elements. `beginRender` → `CRenderPass::clear()` then calls our destructor after `dlclose`. That is a session-killing SIGSEGV.
- Function hooks are x86_64-only and a last resort. We use `Event::bus()`.
- Hyprland headers are C++26-shaped. The Makefile asks for `gnu++26`.
- Omarchy `~/.config/omarchy/plugins/` is Quickshell. Putting this there does nothing.

## Next, when you pick it up

1. Confirm `mise run build` produces `hypr-shiny-border.so`.
2. Nest, reload, move the mouse. If the gradient is "sideways", bump `angle_offset` by 90.
3. If it tracks but stutters, raise `quantize_deg` to 3–5.
4. Conic / nearest-edge highlight is a custom fragment shader, not another `CGradientValueData`. Linear is the ceiling of `renderBorder`.
5. A Rust crate for the math is optional spite. Keep `CShinyBorder` in C++.

## License

Write one when it does something you would ship. Until then it is a toy aimed at Vaxry's blood pressure.

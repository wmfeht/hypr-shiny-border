# hypr-shiny-border

Window borders that look at the mouse.

Hyprland already does gradient borders. It already spins them with `borderangle loop` if you enjoy melting your battery. This plugin is the other trick: a **conic highlight on the ring faces the cursor**, relative to the window.

This is a Hyprland compositor plugin. It is **not** an Omarchy-shell plugin. The bar cannot draw window decorations. If you clone `omarchy.clock` you will get a clock.

Target: **Hyprland v0.56.2** (the one Omarchy has installed). Headers live in `/usr/include/hyprland`. If `mise run headers` shows two different git hashes, stop and rebuild.

Status: it loads, it tracks, it draws. Custom GLES 3 fragment shader (`src/shaders.hpp`) — a comet on a rounded-rect SDF ring, with an optional pulse. If the shader fails to compile, draw falls back to Hyprland’s linear `CBorderPassElement`.

## Why a plugin

| Approach | What you get | What you pay |
|---|---|---|
| Lua timer + `set_prop` | works, ~40 lines | polls at 16 ms, no pointer event in Lua |
| `borderangle loop` | rainbow vomit | full refresh-rate redraw even when idle |
| **This** | real `input.mouse.move`, damage only the ring | C++ against an unstable ABI |

Vaxry, on Rust: *"no ABI, and a slow ass compiler."* The Hyprland plugin API is C++ objects with C symbol names on purpose. The `.so` is C++.

## How it is wired

```
mouse.move  ──►  atan2(cursor - window.middle())
                 quantize to N degrees
                 CShinyBorder::setAngle
                 damageEntire()            ── only if the angle changed

window.open ──►  addWindowDecoration(CShinyBorder)

render.pre  ──►  damageEntire() if pulse is on (the ring breathes)

draw()      ──►  CShinyPassElement (conic comet shader)
                 else CBorderPassElement linear gradient
```

`CShinyBorder` is a custom `IHyprWindowDecoration`. That is the borders-plus-plus pattern: we own the draw path so we do not poke `PHLWINDOW->m_realBorderColor.m_angle`. That field already moved onto the decoration on git `main`. Touching it is how plugins die across one Hyprland bump.

Stock `borderangle` / `border` animations must stay **off** while this runs, or Hyprland interpolates the color and the highlight lags the mouse. The nest config disables both.

`active_only` (default on) means only the focused window tracks. Pointing every visible window at the cursor is N border damages per motion event.

## Config

Hyprland Lua rewrites `-` to `_` in config keys. The C++ names are `plugin:shiny-border:…`. The Lua table is **`shiny_border`**. `plugin = { ["shiny-border"] = { … } }` is an unknown key and does nothing.

Plugin keys do not exist until `PLUGIN_INIT`. Gate on `hl.get_loaded_plugins()`, then `PLUGIN_INIT` calls `reloadConfig()` and the gate opens.

```lua
hl.config({
  general = { border_size = 0 }, -- stock ring off; we draw ours
})

local function shinyLoaded()
  for _, p in ipairs(hl.get_loaded_plugins()) do
    if p.name == "hypr-shiny-border" then
      return true
    end
  end
  return false
end

if shinyLoaded() then
  hl.config({
    plugin = {
      shiny_border = {
        enabled      = true,
        active_only  = true,
        pulse        = true,
        pulse_hz     = 0.4,  -- oscillation rate
        lobe         = 0.18, -- highlight half-width as a fraction of the circle
        quantize_deg = 1,    -- 5 is cheaper if it chatters
        angle_offset = 0,    -- add 90 if the head faces the wrong way
        border_size  = 1,    -- px; plugin default is 3; -1 = general:border_size
      },
    },
  })
end
```

`col.a` / `col.b` are ARGB. Defaults match Omarchy’s `rgba(33ccffee)` / `rgba(00ff99ee)`.

If you leave `general:border_size` at 2 you will get **two** rings: Hyprland’s plus ours. Zero the stock one.

## Running it on the login session

`pluginctl` copies the `.so` to `/tmp` and **refuses instance 0**. That is the nest path. For a session that survives reboot, install a stable copy and load it from config:

```sh
mkdir -p ~/.local/lib/hypr
cp hypr-shiny-border.so ~/.local/lib/hypr/hypr-shiny-border.so
```

```lua
-- ~/.config/hypr/autostart.lua
local SHINY_SO = (os.getenv("HOME") or "") .. "/.local/lib/hypr/hypr-shiny-border.so"

hl.on("hyprland.start", function()
  for _, p in ipairs(hl.get_loaded_plugins()) do
    if p.name == "hypr-shiny-border" then
      return
    end
  end
  local f = io.open(SHINY_SO, "r")
  if not f then
    return
  end
  f:close()
  hl.plugin.load(SHINY_SO)
end)
```

After a rebuild, recopy the `.so` or login still runs the old one. After an `omarchy update` / Hyprland bump, `PLUGIN_INIT` refuses a hash mismatch — rebuild against the new headers, recopy, reload.

A plugin crash is a compositor crash. Iterate in the nest. The login session is for a `.so` you already trust.

## Dev cycle

```text
mise run nest          # nested Hyprland in a window, nest/hyprland.lua
                       # ALT+Return = foot, ALT+Q = close, ALT+M = kill nest

# outer terminal:
mise run headers       # hashes must match
mise run reload        # make + copy to /tmp + unload/load into the *last* instance
```

`scripts/pluginctl.sh` copies the `.so` to a new `/tmp` path on every load so `dlopen` cannot keep a stale mapping of the same filename. Override into the live session only if you mean it:

```
SHINY_INSTANCE=0 SHINY_LIVE=1 mise run load
```

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
src/pass.*       CShinyPassElement, shader compile
src/teardown.*   teardown mark + shader lifecycle (PLUGIN_EXIT)
src/runtime.*    mapped-window / VAO guards (damageEntire, shader draw)
src/shaders.hpp  vertex + conic comet fragment
src/globals.hpp  HANDLE, config SPs, signal listeners (must stay alive)
tests/           unit tests (no compositor)
nest/hyprland.lua
scripts/pluginctl.sh
hyprpm.toml      packaging later; not the inner loop
mise.toml        tasks
```

`hyprpm` is how other people install this. `make` + `hyprctl plugin load` (or `hl.plugin.load`) is the loop.

## Landmines

- **Same compiler as Hyprland.** GCC 16.2.1 on this box. mise is not allowed to hand you a different g++.
- **`--no-gnu-unique`** (`-fno-gnu-unique`) or unload is a lie.
- **Hash check** in `PLUGIN_INIT`. If you skip it you get a "fun" SIGSEGV after the next `omarchy update`.
- **Lua key is `shiny_border`.** Hyphens become underscores. `["shiny-border"]` is unknown.
- **Do not** `removeAllOfType("CBorderPassElement")` on exit. That pass is also the stock border.
- **Do** `m_renderPass.clear()` on the leftover already-rendered pass, and reset `Event::bus()` listeners, in `PLUGIN_EXIT`. Surgical `removeAllOfType("CShinyPassElement")` does not recurse into nested transformer passes. Hyprland strips decorations; it does **not** flush plugin pass elements. `beginRender` → `CRenderPass::clear()` then calls our destructor after `dlclose`. That is a session-killing SIGSEGV.
- Function hooks are x86_64-only and a last resort. We use `Event::bus()`.
- Hyprland headers are C++26-shaped. The Makefile asks for `gnu++26`.
- Omarchy `~/.config/omarchy/plugins/` is Quickshell. Putting this there does nothing.

## License

Still a toy. Write one when you would ship it.

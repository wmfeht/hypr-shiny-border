# hypr-shiny-border

A Hyprland plugin that draws a window border whose highlight faces the cursor.

It is a compositor plugin (a C++ `.so` loaded into Hyprland). It is not an
Omarchy-shell / Quickshell plugin — the bar cannot draw window decorations.

**Target:** Hyprland **v0.56.2**. Build with the same compiler that built the
compositor, against the headers in `/usr/include/hyprland`. `mise run headers`
must show the same git hash for `hyprctl version` and `version.h`.

## What it does

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

`active_only` (default on) means only the focused window tracks the cursor and
pulses. Stock `border` / `borderangle` animations must stay off, or Hyprland
interpolates the color and the highlight lags the mouse. Zero
`general:border_size` as well — otherwise you get two rings.

A plugin crash is a compositor crash. Develop in the nested session.

## Build

Needs Hyprland headers, `pkg-config`, and the compositor’s `g++` (`gnu++26`,
`-fno-gnu-unique`).

```sh
mise run build     # make -j$(nproc) → hypr-shiny-border.so
mise run test      # compositor-free unit tests
mise run headers   # running compositor hash vs installed headers
```

## Install (login session)

`mise run load` / `pluginctl` copies the `.so` to `/tmp` and **refuses
instance 0**. That is the nest path.

For a session that survives reboot:

```sh
mise run install   # ~/.local/lib/hypr/hypr-shiny-border.so
```

```lua
-- ~/.config/hypr/autostart.lua
local SHINY_SO = (os.getenv("HOME") or "") .. "/.local/lib/hypr/hypr-shiny-border.so"

local function shinyLoaded()
  for _, p in ipairs(hl.get_loaded_plugins()) do
    if p.name == "hypr-shiny-border" then
      return true
    end
  end
  return false
end

hl.on("hyprland.start", function()
  if shinyLoaded() then
    return
  end
  local f = io.open(SHINY_SO, "r")
  if not f then
    return
  end
  f:close()
  hl.plugin.load(SHINY_SO)
end)
```

After a rebuild, recopy (`mise run install`) or login still runs the old
binary. After a Hyprland bump, `PLUGIN_INIT` refuses a header-hash mismatch —
rebuild, recopy, reload.

`hyprpm.toml` is in the tree for later. The loop today is `make` +
`hyprctl plugin load` / `hl.plugin.load`.

## Config

Hyprland Lua rewrites `-` to `_` in config keys. C++ registers
`plugin:shiny-border:…`; the Lua table is **`shiny_border`**.
`plugin = { ["shiny-border"] = { … } }` is an unknown key and does nothing.

Those keys do not exist until `PLUGIN_INIT`. Gate on `hl.get_loaded_plugins()`.
`PLUGIN_INIT` then calls `reloadConfig()`, and the gate opens.

```lua
hl.config({
  general = { border_size = 0 }, -- stock ring off; the plugin draws its own
})

hl.animation({ leaf = "border", enabled = false })
hl.animation({ leaf = "borderangle", enabled = false })

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
        col = {
          a = "rgba(33ccffee)", -- highlight head
          b = "rgba(00ff99ee)", -- shoulder
        },
      },
    },
  })
end
```

`col.a` / `col.b` are ARGB. Defaults match Omarchy’s active gradient.

| Key | Default | |
|---|---|---|
| `enabled` | `true` | master switch |
| `active_only` | `true` | only the focused window tracks / pulses |
| `pulse` | `true` | breathe highlight width and thickness |
| `pulse_hz` | `0.4` | oscillation rate |
| `lobe` | `0.18` | highlight half-width (fraction of the circle) |
| `quantize_deg` | `1` | snap heading; larger is cheaper |
| `angle_offset` | `0` | degrees added to `atan2` |
| `border_size` | `3` | px; `-1` follows `general:border_size` |
| `col.a` | `rgba(33ccffee)` | comet head |
| `col.b` | `rgba(00ff99ee)` | comet shoulder |

## Development

```text
mise run nest          # nested Hyprland, nest/hyprland.lua
                       # ALT+Return = foot, ALT+Q = close, ALT+M = kill nest

# outer terminal, nest already up:
mise run reload        # make + copy to a fresh /tmp path + unload/load
                       # last hyprctl instance; refuses the login session
```

`scripts/pluginctl.sh` copies the `.so` to a new `/tmp` path on every load so
`dlopen` cannot keep a stale mapping of the same filename.

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

```
src/main.cpp     PLUGIN_INIT / EXIT, listeners, heading
src/deco.*       IHyprWindowDecoration, pulse timer
src/pass.*       CShinyPassElement, shader compile
src/teardown.*   teardown mark + shader lifecycle
src/runtime.*    mapped-window / VAO guards
src/shaders.hpp  vertex + conic comet fragment
tests/           unit tests (no compositor)
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
- Putting this under `~/.config/omarchy/plugins/` does nothing.

## License

MIT. See [LICENSE](LICENSE).

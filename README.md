# hypr-shiny-border

A Hyprland plugin that draws a window border whose highlight faces the cursor.

Hyprland can already paint a gradient ring and spin it with `borderangle`, but
that highlight is not tied to the pointer. This plugin draws its own ring: a
conic comet on a rounded rect, aimed at the mouse, so the focused window looks
like it is catching a light from wherever you are pointing. Unfocused windows
keep the same padding and show no ring, so focus does not reflow the client.

It is a compositor plugin (a C++ `.so` loaded into Hyprland), not an
Omarchy-shell / Quickshell plugin. Putting it under
`~/.config/omarchy/plugins/` does nothing.

**Target:** Hyprland **v0.56.2**.

## Use

A **1px** ring on its own is a light sheen around the focused window. That is
the look this is meant for.

For something quieter, keep the 1px ring and turn Hyprland’s **drop shadow**
on. The shadow carries the depth; the comet is just a glint on the edge.

Turn the stock border off (`general.border_size = 0`) or you get two rings.
Disable the `border` and `borderangle` animations or Hyprland interpolates the
color and the highlight lags the mouse.

`active_only` (default on) means only the focused window tracks the cursor and
pulses. `enabled = false` reserves 0 px, so turning the plugin off does not
leave a gap.

Hacking on the plugin itself is in [DEVELOPMENT.md](DEVELOPMENT.md).

## Install

Use [`hyprpm`](https://wiki.hypr.land/Plugins/Using-Plugins/#hyprpm). It clones
this repo, builds `hypr-shiny-border.so` against your running Hyprland commit,
and loads it through `hyprctl plugin load`.

`hyprpm add` clones the **default branch** (`main`). Pin a Hyprland commit to a
plugin commit in `hyprpm.toml` after you cut a release; with no matching pin,
hyprpm builds `HEAD`.

```sh
hyprpm update
hyprpm add https://github.com/wmfeht/hypr-shiny-border
hyprpm enable hypr-shiny-border
hyprpm reload -n
```

`hyprctl plugin list` should then show `hypr-shiny-border`. Enable/disable is
`hyprpm enable hypr-shiny-border` / `hyprpm disable hypr-shiny-border`. After a
Hyprland bump, `hyprpm update` rebuilds against the new headers. `PLUGIN_INIT`
still refuses a header-hash mismatch.

Load at login. If you use
[permission management](https://wiki.hypr.land/Configuring/Advanced-and-Cool/Permissions),
allow hyprpm to load plugins or you get a popup every start:

```lua
-- ~/.config/hypr/autostart.lua
hl.permission("/usr/(bin|local/bin)/hyprpm", "plugin", "allow")
hl.exec_once("hyprpm reload -n")
```

Do not also `hl.plugin.load` the same plugin — hyprpm already asks hyprctl to
load it.

### Manual (`hyprctl`)

Build the `.so` yourself and pass an **absolute** path:

```sh
make -j$(nproc)
hyprctl plugin load $PWD/hypr-shiny-border.so
hyprctl plugin list
hyprctl plugin unload $PWD/hypr-shiny-border.so
```

To keep a login-session copy without hyprpm:

```sh
mkdir -p ~/.local/lib/hypr
cp -f hypr-shiny-border.so ~/.local/lib/hypr/hypr-shiny-border.so
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

After a rebuild, recopy or login still runs the old binary.

## Config

Hyprland Lua rewrites `-` to `_` in config keys. C++ registers
`plugin:shiny-border:…`; the Lua table is **`shiny_border`**.
`plugin = { ["shiny-border"] = { … } }` is an unknown key and does nothing.

Those keys do not exist until `PLUGIN_INIT`. Gate on `hl.get_loaded_plugins()`.
`PLUGIN_INIT` then calls `reloadConfig()`, and the gate opens.

```lua
hl.config({
  general = { border_size = 0 }, -- stock ring off; the plugin draws its own
  decoration = {
    -- Optional: let the shadow do the depth, keep the ring at 1px.
    -- shadow = { enabled = true },
  },
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
        pulse_hz     = 0.4,  -- 0 disables oscillation (same as pulse = false for the timer)
        lobe         = 0.18, -- highlight half-width as a fraction of the circle
        quantize_deg = 1,    -- snap heading; applies while pulse is on; 5 is cheaper
        angle_offset = 0,    -- degrees added to the comet heading; 90 if the head is flipped
        border_size  = 1,    -- 1px sheen; plugin default is 3; -1 = general:border_size
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
| `enabled` | `true` | master switch; `false` reserves 0 px (no gap) |
| `active_only` | `true` | only the focused window tracks / pulses; unfocused keep the padding, no ring |
| `pulse` | `true` | breathe highlight width and thickness |
| `pulse_hz` | `0.4` | oscillation rate; `0` disables (same as `pulse = false` for the timer) |
| `lobe` | `0.18` | highlight half-width (fraction of the circle) |
| `quantize_deg` | `1` | snap heading to this many degrees; applies while pulse is on; larger is cheaper |
| `angle_offset` | `0` | degrees added to the comet heading (shader and fallback) |
| `border_size` | `3` | px; `-1` follows `general:border_size`. 1px is the intended sheen |
| `col.a` | `rgba(33ccffee)` | comet head |
| `col.b` | `rgba(00ff99ee)` | comet shoulder |

## License

MIT. See [LICENSE](LICENSE).

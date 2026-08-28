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

Three optional looks on top of the comet:

- **Gradient** (`gradient = { colors = { … } }`): replace the two-color
  `col.a`/`col.b` ramp with a multi-step gradient. List as many colors as you
  want steps (up to 8): the first color is the comet head, the last sits on
  the far side of the ring, mirrored on both sides of the heading. Fewer than
  two colors keeps the classic look. The gradient's own angle is ignored —
  the heading still follows the mouse (or `pin_deg`). Steps are evenly
  spaced unless `gradient_positions` places them: one percentage of the total
  ramp length per color (`"0 70 100"` gives the first step 70% of the ramp
  and the second the remaining 30%). By default both halves of the ring
  mirror each other; `gradient_cw` / `gradient_positions_cw` give the
  clockwise half its own colors and/or step lengths.
- **Shimmer** (`shimmer = true`): instead of breathing in place, the highlight
  wanders randomly around its heading (within `shimmer_deg`) and randomly
  resizes (within `shimmer_scale_min`..`shimmer_scale_max`). The two walks
  retarget on independent clocks, so drift and resize are visibly decoupled.
  Shimmer is exclusive with `pulse` — when both are on, shimmer wins.
- **Pin** (`pin = true`): the highlight stops following the mouse and stays at
  `pin_deg` (degrees CCW, `0` = right, `90` = up). Pulse or shimmer still
  animate around the pinned heading.

Hacking on the plugin itself is in [DEVELOPMENT.md](DEVELOPMENT.md).

## Install

Use [`hyprpm`](https://wiki.hypr.land/Plugins/Using-Plugins/#hyprpm). It clones
this repo, builds `hypr-shiny-border.so` against your running Hyprland commit,
and loads it through `hyprctl plugin load`.

`hyprpm add` clones the **default branch** (`main`). `hyprpm.toml` pins
Hyprland **v0.56.2** (`efb50993780079460b0cbed1363e2166a2de1d9f`) to plugin
commit `50669e41b7c12e6371b6158c654330e41fd3c4fa`. If no pin matches, hyprpm
builds `HEAD`.

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
hl.permission({ binary = "/usr/(bin|local/bin)/hyprpm", type = "plugin", mode = "allow" })
hl.on("hyprland.start", function()
  hl.exec_cmd("hyprpm reload -n")
end)
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
        -- shimmer instead of pulse (shimmer wins when both are on):
        -- shimmer           = true,
        -- shimmer_hz        = 0.6,  -- average retargets per second; 0 disables
        -- shimmer_deg       = 25,   -- max wander each side of the heading
        -- shimmer_scale_min = 0.75, -- highlight size scale bounds
        -- shimmer_scale_max = 1.35,
        -- pinned heading instead of the mouse:
        -- pin     = true,
        -- pin_deg = 90,             -- degrees CCW; 0 = right, 90 = up
        col = {
          a = "rgba(33ccffee)", -- highlight head
          b = "rgba(00ff99ee)", -- shoulder
        },
        -- multi-step ramp instead of col.a/col.b (head first, up to 8):
        -- gradient = { colors = { "rgba(33ccffee)", "rgba(00ff99ee)", "rgba(ffcc33ee)" } },
        -- gradient_positions = "0 70 100", -- % of ramp per color; empty = even spacing
        -- clockwise half override (defaults mirror the keys above);
        -- match the first/last colors with gradient's or the ring seams:
        -- gradient_cw = { colors = { "rgba(33ccffee)", "rgba(ff3399ee)", "rgba(ffcc33ee)" } },
        -- gradient_positions_cw = "0 30 100",
      },
    },
  })
end
```

`col.a` / `col.b` are ARGB. Defaults match Omarchy’s active gradient.

`gradient` is a stock Hyprland gradient value, so classic config syntax works
too: `plugin:shiny-border:gradient = rgba(33ccffee) rgba(00ff99ee) rgba(ffcc33ee)`.
Two or more colors switch the ring to the multi-step ramp; one (the default)
keeps `col.a`/`col.b`; anything past 8 colors is ignored. The gradient's
angle field is accepted but unused — the comet aims itself.

`gradient_positions` sizes the steps: one value per gradient color, percent
of the total ramp length, `0` = head, `100` = far side. Space or comma
separated, `%` suffix optional. Stops may not go backwards (a decreasing
value is bumped up to its predecessor); a first stop above `0` or a last
below `100` leaves constant-color bands at the ends. An empty string (the
default), a count that does not match the color count, or an unparsable
token falls back to even spacing.

The ramp is mirrored on both sides of the heading unless you override the
**clockwise half** (looking at the screen, the half you sweep going
clockwise from the comet head). `gradient_cw` gives that half its own
colors — the count may differ from `gradient`'s — and
`gradient_positions_cw` its own step lengths (one percentage per clockwise
color). `gradient_positions_cw` also works on its own: same colors,
different pacing per side. Left unset, each key mirrors the primary one;
with own colors set, an empty `gradient_positions_cw` means even spacing.
The clockwise keys only apply while `gradient` is active.

The two halves always meet at the comet head (first stop) and the far side
(last stop). Nothing enforces that `gradient_cw`'s first and last colors
match `gradient`'s — if they differ, the ring shows a visible seam at the
head and/or the far side. Match the endpoint colors unless you want that.

| Key | Default | |
|---|---|---|
| `enabled` | `true` | master switch; `false` reserves 0 px (no gap) |
| `active_only` | `true` | only the focused window tracks / pulses; unfocused keep the padding, no ring |
| `pulse` | `true` | breathe highlight width and thickness |
| `pulse_hz` | `0.4` | oscillation rate; `0` disables (same as `pulse = false` for the timer) |
| `shimmer` | `false` | randomly wander and resize the highlight; exclusive with `pulse` (shimmer wins) |
| `shimmer_hz` | `0.6` | average shimmer retargets per second; `0` disables |
| `shimmer_deg` | `25` | max shimmer wander each side of the heading, degrees |
| `shimmer_scale_min` | `0.75` | lower bound of the shimmer size scale |
| `shimmer_scale_max` | `1.35` | upper bound of the shimmer size scale |
| `pin` | `false` | pin the highlight to `pin_deg` instead of following the mouse |
| `pin_deg` | `90` | pinned heading, degrees CCW; `0` = right, `90` = up |
| `lobe` | `0.18` | highlight half-width (fraction of the circle) |
| `quantize_deg` | `1` | snap heading to this many degrees; applies while pulse is on; larger is cheaper |
| `angle_offset` | `0` | degrees added to the comet heading (shader, fallback, and pinned) |
| `border_size` | `3` | px; `-1` follows `general:border_size`. 1px is the intended sheen |
| `col.a` | `rgba(33ccffee)` | comet head |
| `col.b` | `rgba(00ff99ee)` | comet shoulder |
| `gradient` | 1 color (off) | multi-step ramp, head first; 2–8 colors replace `col.a`/`col.b`; angle ignored |
| `gradient_positions` | `""` (even) | ramp position per gradient color, % of total length (`"0 70 100"`); empty / mismatch = even spacing |
| `gradient_cw` | 1 color (mirror) | clockwise-half colors; 2–8 colors replace `gradient`'s on that half; match first/last with `gradient` to avoid seams |
| `gradient_positions_cw` | `""` (mirror) | clockwise-half ramp positions, % of total length; empty = mirror `gradient_positions` (or even spacing with own colors) |

## License

MIT. See [LICENSE](LICENSE).

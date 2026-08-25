#!/usr/bin/env bash
# Load/unload/reload hypr-shiny-border.so into a chosen Hyprland instance.
# Default target: last instance in `hyprctl instances` (the nest, if you have one).
#
# The live session is instance 0. Loading a half-baked plugin there takes down
# the compositor. That is refused unless you set both:
#   SHINY_INSTANCE=0 SHINY_LIVE=1
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SO="$ROOT/hypr-shiny-border.so"
STATE="/tmp/hypr-shiny-border.lastso"

die() { echo "pluginctl: $*" >&2; exit 1; }

instance_count() {
  hyprctl instances -j 2>/dev/null | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))' 2>/dev/null || echo 1
}

# Instance 0 is the login session. Anything else is a nest.
refuse_live() {
  die "refusing to touch the live Hyprland session.

pluginctl: a plugin crash is a compositor crash (SIGSEGV in CRenderPass::clear
pluginctl: last time, 2026-08-24 20:59). Iterate in a nest:

pluginctl:   mise run nest          # nested Hyprland, ALT+M to kill it
pluginctl:   mise run reload        # from an outer terminal, while the nest is up

pluginctl: to override, and only if you mean it:
pluginctl:   SHINY_INSTANCE=0 SHINY_LIVE=1 mise run load"
}

instance() {
  if [[ -n "${SHINY_INSTANCE:-}" ]]; then
    if [[ "${SHINY_INSTANCE}" == "0" && "${SHINY_LIVE:-}" != "1" ]]; then
      refuse_live
    fi
    echo "$SHINY_INSTANCE"
    return
  fi
  local count
  count="$(instance_count)"
  if [[ "$count" -le 1 ]]; then
    refuse_live
  fi
  echo $((count - 1))
}

hc() {
  local i
  i="$(instance)"
  hyprctl -i "$i" "$@"
}

cmd="${1:-}"
case "$cmd" in
  load)
    [[ -f "$SO" ]] || die "no $SO — run: mise run build"
    # Resolve the target *before* copying, so a refuse doesn't leave a stray .so.
    target="$(instance)"
    dest="/tmp/hypr-shiny-border-$$.so"
    cp -f "$SO" "$dest"
    echo "$dest" > "$STATE"
    hyprctl -i "$target" plugin load "$dest"
    hyprctl -i "$target" plugin list
    ;;
  unload)
    target="$(instance)"
    if [[ -f "$STATE" ]]; then
      hyprctl -i "$target" plugin unload "$(cat "$STATE")" || true
      rm -f "$STATE"
    else
      echo "pluginctl: nothing recorded; trying the tree .so" >&2
      hyprctl -i "$target" plugin unload "$SO" || true
    fi
    ;;
  reload)
    "$0" unload || true
    "$0" load
    ;;
  *)
    die "usage: pluginctl.sh {load|unload|reload}"
    ;;
esac

#!/usr/bin/env bash
# Cloud Agent bootstrap for hypr-shiny-border.
#
# Scope: this prepares the compositor-free development loop that runs on a
# headless VM — a gnu++26 toolchain, pkg-config, the pixman/libdrm/pangocairo
# dev headers the Makefile queries, and the `mise` task runner that wraps make.
#
# It deliberately does NOT install Hyprland. Building hypr-shiny-border.so and
# running `make test-full` / the nested-Hyprland loop need Hyprland v0.56.2
# internal headers plus a live GPU/DRM-backed compositor, which a headless
# Cloud Agent cannot provide. `make test` (the compositor-free logic tests)
# is the loop this environment supports.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

sudo apt-get update -qq
# gcc-14/g++-14: the project builds with -std=gnu++26, unsupported by the
# distro-default gcc-13. The dev libs are the non-Hyprland packages the
# Makefile's pkg-config line references.
sudo apt-get install -y -qq --no-install-recommends \
  gcc-14 g++-14 make pkg-config \
  libpixman-1-dev libdrm-dev libpango1.0-dev \
  curl ca-certificates

# Point the default gcc/g++ at 14 so `make` (CXX ?= g++) gets gnu++26.
# update-alternatives is safe to re-run; it just re-registers the same entry.
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 60 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-14

# mise task runner (mise.toml wraps make). Install to a system path so every
# shell finds it without editing shell profiles.
if ! command -v mise >/dev/null 2>&1; then
  curl -fsSL https://mise.run -o /tmp/mise-install.sh
  sudo env MISE_INSTALL_PATH=/usr/local/bin/mise sh /tmp/mise-install.sh
  rm -f /tmp/mise-install.sh
fi

# Trust this repo's mise config so `mise run <task>` is non-interactive.
mise trust "$PWD/mise.toml" >/dev/null 2>&1 || true

echo "hypr-shiny-border cloud env ready: $(g++ --version | head -1)"

# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/overlays.nix — the SINGLE source of truth for the PlasmaZones
# package. Every other flake output (packages, devShells, checks, formatter)
# and every consumer (NixOS module, Home Manager module, third-party
# `nixpkgs.overlays` users) resolves the package through THIS overlay, so the
# build is wired up in exactly one place instead of five separate call sites.
#
# ── THE KWIN IID CONSTRAINT (why an overlay is the right shape) ────────────────
# PlasmaZones ships a KWin C++ effect plugin (kwin-effect/). KWin embeds its own
# exact version string into every plugin it loads as an IID (Interface
# Identifier) and refuses, at runtime, to load any plugin whose IID does not
# match the running KWin — even across patch releases (6.6.4 vs 6.6.5). So the
# `kwin` used to COMPILE the plugin must equal the `kwin` the user is RUNNING.
#
# An overlay satisfies this by construction: it builds against `final` — the
# consumer's fully-overlaid package set — so `final.kdePackages.kwin` is always
# the host's kwin. NixOS / Home Manager modules and `nixpkgs.overlays` users
# therefore get a plugin whose IID matches their compositor automatically.
#
# The flake's own `packages.default` is built by extending the flake's pinned
# nixpkgs with this overlay (see flake.nix `pkgsFor`). That path is pinned to
# the flake's nixpkgs and is the documented "risky" install path: fine for CI
# and `nix build`, but `nix profile install` can drift from a rolling system's
# kwin. The safe paths are the module/overlay outputs, which use host pkgs.
{ src, version }:

final: _prev: {
  plasmazones = final.callPackage ./package.nix {
    inherit src version;
  };
}

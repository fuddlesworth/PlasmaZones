# SPDX-License-Identifier: GPL-3.0-or-later
#
# flake.nix — PlasmaZones. Thin wiring only. The build recipe lives in
# packaging/nix/package.nix and the package is defined once in
# packaging/nix/overlays.nix; see that file for the KWin-IID rationale and why
# consumers must build against their own pkgs.
#
# Outputs:
#   packages.{default,plasmazones} — built against THIS flake's pinned nixpkgs.
#                                    Fine for `nix build` / CI; `nix profile
#                                    install` may drift from a rolling kwin.
#   overlays.default               — adds `pkgs.plasmazones`, built against the
#                                    consumer's pkgs so the KWin IID matches.
#   nixosModules.default           — programs.plasmazones.enable (host pkgs).
#   homeManagerModules.default     — programs.plasmazones.enable (host pkgs).
#   devShells.default              — `nix develop`.
#   formatter.default              — `nix fmt` (Nix + C++ + QML).
#   checks.package                 — `nix flake check`.
{
  description = "FancyZones-style window tiling and autotiling for KDE Plasma 6.7+";

  inputs = {
    # Normally tracks nixos-unstable for the most current KDE Frameworks and
    # KWin packages. Temporarily pinned to the nixpkgs master commit that landed
    # Plasma 6.7 / KWin 6.7 (NixOS/nixpkgs#520160) because the nixos-unstable
    # channel still ships KWin 6.6.5 and our kwin-effect requires the 6.7 effect
    # API. Revert to "github:NixOS/nixpkgs/nixos-unstable" once the channel has
    # advanced past Plasma 6.7 (the weekly update-flake-lock job will do this).
    # If you need a stable channel, use nixos-24.11 or later, but be aware that
    # stable channels may lag behind upstream KDE releases.
    nixpkgs.url = "github:NixOS/nixpkgs/c190319055bb5c31acfd7bb8356ce9ab05cb2b36";
  };

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;

      # PlasmaZones is Wayland/Linux-only. These are the two supported arches
      # (kept explicit so `nix flake show` never references a system nixpkgs
      # lacks a legacyPackages set for).
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = lib.genAttrs supportedSystems;

      # Version single-source-of-truth: parsed once from the top-level project()
      # in CMakeLists.txt. `self` is always a store path here, so this read is
      # pure and never triggers import-from-derivation (reading it inside
      # package.nix would, when nixpkgs builds from a fetchFromGitHub src). The
      # regex is anchored to the literal `PlasmaZones` project so it cannot match
      # a subdirectory's own project() declaration.
      version =
        let
          cmake = builtins.readFile "${self}/CMakeLists.txt";
          m = builtins.match ".*project\\(PlasmaZones VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*" cmake;
        in
        if m != null then
          builtins.head m
        else
          throw "plasmazones: could not parse VERSION from CMakeLists.txt";

      # Scope the build source so edits to docs / CI / flake files don't
      # invalidate the build. `self` is already git-clean (build/, .claude/ are
      # gitignored); this additionally drops tracked-but-build-irrelevant paths.
      src = lib.fileset.toSource {
        root = ./.;
        fileset = lib.fileset.difference ./. (
          lib.fileset.unions [
            ./.github
            ./docs
            ./examples
            ./README.md
            ./CHANGELOG.md
            ./CONTRIBUTING.md
            ./CLAUDE.md
            ./.copr
            ./Dockerfile
            ./.dockerignore
            ./lefthook.yml
            ./flake.nix
            ./flake.lock
          ]
        );
      };

      # Single source of truth for the package (see packaging/nix/overlays.nix).
      overlay = import ./packaging/nix/overlays.nix { inherit src version; };

      # The flake's pinned nixpkgs with our overlay applied, per system. Every
      # package / devShell / check / formatter is derived from this, so the
      # build recipe is wired up in exactly one place.
      pkgsFor = forAllSystems (system: nixpkgs.legacyPackages.${system}.extend overlay);
    in
    {
      overlays.default = overlay;

      packages = forAllSystems (system: {
        default = pkgsFor.${system}.plasmazones;
        plasmazones = pkgsFor.${system}.plasmazones; # named alias
      });

      # `nix flake check`. Verifies the package builds. Tests are disabled in
      # the package build (they need a Wayland compositor; see package.nix).
      checks = forAllSystems (system: {
        package = self.packages.${system}.default;
      });

      devShells = forAllSystems (system: {
        default = pkgsFor.${system}.callPackage ./packaging/nix/devShell.nix { };
      });

      formatter = forAllSystems (system: pkgsFor.${system}.callPackage ./packaging/nix/formatter.nix { });

      nixosModules.default = import ./packaging/nix/module.nix { inherit src version; };
      homeManagerModules.default = import ./packaging/nix/hm-module.nix { inherit src version; };
    };
}

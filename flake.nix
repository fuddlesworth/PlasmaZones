# SPDX-License-Identifier: GPL-3.0-or-later
#
# flake.nix — PlasmaZones Nix flake
#
# WHAT THIS FLAKE PROVIDES
# ────────────────────────
#  packages.default / packages.plasmazones
#    The PlasmaZones package built against this flake's pinned nixpkgs.
#    Correct for `nix build` and `nix profile install`. See the WARNING
#    below about the KWin IID constraint before installing this way.
#
#  nixosModules.default
#    A NixOS module (programs.plasmazones.enable = true). Builds the package
#    against the *host* system's pkgs so the KWin plugin IID always matches.
#    This is the recommended install path on NixOS.
#
#  homeManagerModules.default
#    A Home Manager module for per-user installs without NixOS. Also builds
#    against the host's pkgs for the same IID reason.
#
#  overlays.default
#    A nixpkgs overlay that adds `pkgs.plasmazones`. Builds against the
#    consumer's `final` pkgs — correct by construction.
#
#  devShells.default
#    A development shell with all build dependencies and debugging tools.
#
# THE KWIN IID CONSTRAINT (why "host pkgs" matters so much)
# ──────────────────────────────────────────────────────────
# PlasmaZones includes a KWin C++ effect plugin (kwin-effect/). KWin embeds
# its own version string in every plugin it loads as an IID (Interface
# Identifier). At runtime, KWin checks that the IID of any plugin it loads
# matches the running KWin's own version string *exactly* — even a patch
# version mismatch (6.7.0 vs 6.7.1) causes KWin to silently refuse the plugin.
#
# This means: the `kwin` package used to *compile* the plugin must be the
# same `kwin` the user is *running*. On a rolling nixos-unstable system,
# kwin moves often. If you use `nix profile install` (which pins to THIS
# flake's nixpkgs), and your system's kwin has since updated, the plugin
# will not load and zone overlays will not appear — but everything else works.
#
# SAFE INSTALL PATHS (use these):
#   • NixOS module  → builds against your system pkgs every rebuild ✓
#   • Home Manager module → same ✓
#   • overlay  → same ✓
#
# RISKY INSTALL PATH (you're warned):
#   • `nix profile install github:fuddlesworth/PlasmaZones`
#     Works if the flake.lock kwin == your running kwin, breaks silently
#     if they diverge. Re-run `nix flake update` + reinstall after any
#     system plasma/kwin update to stay in sync.
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

  outputs = { self, nixpkgs }:
    let
      # The two Linux architectures PlasmaZones supports.
      # macOS and Windows are not supported — PlasmaZones is Wayland/Linux-only.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];

      # Helper: produce an attribute set { x86_64-linux = f "x86_64-linux"; ... }
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

      # ── Core build function ────────────────────────────────────────────────
      # mkPlasmaZones takes a nixpkgs `pkgs` instance and returns the built
      # package. The caller is responsible for passing the RIGHT pkgs:
      #
      #   • For NixOS/HM modules and overlays: pass the host's pkgs (the `pkgs`
      #     argument Nix injects into the module). This ensures the kwin package
      #     used for compilation matches the kwin the user is running.
      #
      #   • For the flake's own `packages` output: we pass the flake's pinned
      #     nixpkgs, which is fine for CI and `nix build` but may misalign with
      #     a rolling system's kwin (see THE KWIN IID CONSTRAINT above).
      #
      # The actual build recipe lives in packaging/nix/package.nix. Keeping it
      # separate makes it easy to callPackage from nixpkgs upstream later.
      mkPlasmaZones = pkgs:
        pkgs.callPackage ./packaging/nix/package.nix {
          # Pass the repo root as the source. When building from a flake input,
          # `self` is the root of the repository. When building from nixpkgs,
          # this argument is overridden with a fetchFromGitHub derivation.
          src = self;
        };

    in
    {
      # ── packages ────────────────────────────────────────────────────────────
      # Exposed for `nix build`, `nix profile install`, and CI.
      # Built against the flake's pinned nixpkgs — see the IID warning above.
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default    = mkPlasmaZones pkgs;
          plasmazones = self.packages.${system}.default;  # named alias
        }
      );

      # ── checks ──────────────────────────────────────────────────────────────
      # Run with `nix flake check`. Currently just verifies the package builds
      # without C++ compilation errors. Tests are disabled in the package build
      # (they need a Wayland compositor and cannot run in the Nix sandbox).
      checks = forAllSystems (system: {
        package = self.packages.${system}.default;
      });

      # ── NixOS module ────────────────────────────────────────────────────────
      # Recommended install path on NixOS. Add to your configuration:
      #
      #   inputs.plasmazones.url = "github:fuddlesworth/PlasmaZones";
      #
      #   { inputs, ... }: {
      #     imports = [ inputs.plasmazones.nixosModules.default ];
      #     programs.plasmazones.enable = true;
      #   }
      #
      # After `nixos-rebuild switch`, enable the daemon for your user:
      #   systemctl --user enable --now plasmazones.service
      #
      # And (once, or after any Plasma update) refresh the KDE service cache:
      #   kbuildsycoca6 --noincremental
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.plasmazones;
        in
        {
          options.programs.plasmazones = {
            enable = lib.mkEnableOption
              "PlasmaZones window tiling for KDE Plasma 6.7+";

            package = lib.mkOption {
              type        = lib.types.package;
              # Build against the HOST's pkgs, not the flake's pinned pkgs.
              # This is what keeps the KWin effect IID in sync with the running KWin.
              default     = mkPlasmaZones pkgs;
              defaultText = lib.literalExpression
                ''pkgs.callPackage "${self}/packaging/nix/package.nix" { src = ${self}; }'';
              description = ''
                The PlasmaZones package to use. The default builds against the
                host system's pkgs so the KWin effect plugin's IID matches the
                running KWin binary. Override only if you know what you're doing.
              '';
            };
          };

          config = lib.mkIf cfg.enable {
            # Install the package system-wide so all users can run it.
            environment.systemPackages = [ cfg.package ];

            # Refresh the KDE service database once at activation so the KCM
            # (System Settings → Window Management → PlasmaZones) appears
            # immediately after `nixos-rebuild switch` without requiring a logout.
            #
            # kbuildsycoca6 scans *.desktop files in XDG_DATA_DIRS and rebuilds
            # the sycoca cache that KDE uses to discover plugins and services.
            system.userActivationScripts.plasmazones-sycoca = {
              text = ''
                # Only run if kbuildsycoca6 is available (i.e. KDE is installed)
                if command -v kbuildsycoca6 >/dev/null 2>&1; then
                  kbuildsycoca6 --noincremental 2>/dev/null || true
                fi
              '';
              # Run after the package is deployed to $PATH
              deps = [];
            };

            # The systemd user service is shipped by the package itself
            # (installed to lib/systemd/user/plasmazones.service).
            # Users start it with:
            #   systemctl --user enable --now plasmazones.service
            #
            # We intentionally do NOT auto-enable it here because:
            #   1. The user may want to start it manually after configuring zones.
            #   2. systemd.user services in NixOS modules affect all users on the
            #      system, but PlasmaZones is a per-user tool.
          };
        };

      # ── Home Manager module ─────────────────────────────────────────────────
      # For per-user installs without NixOS (e.g. on a non-NixOS distro with
      # the Nix package manager, or when managing dotfiles with Home Manager).
      #
      # Add to your Home Manager configuration:
      #
      #   inputs.plasmazones.url = "github:fuddlesworth/PlasmaZones";
      #
      #   { inputs, ... }: {
      #     imports = [ inputs.plasmazones.homeManagerModules.default ];
      #     programs.plasmazones.enable = true;
      #   }
      homeManagerModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.plasmazones;
        in
        {
          options.programs.plasmazones = {
            enable = lib.mkEnableOption
              "PlasmaZones window tiling for KDE Plasma 6.7+";

            package = lib.mkOption {
              type        = lib.types.package;
              default     = mkPlasmaZones pkgs;
              defaultText = lib.literalExpression
                ''pkgs.callPackage "${self}/packaging/nix/package.nix" { src = ${self}; }'';
              description = ''
                The PlasmaZones package to use. Built against the host's pkgs
                so the KWin effect plugin IID matches the running KWin.
              '';
            };
          };

          config = lib.mkIf cfg.enable {
            # Install the package into the user's profile.
            home.packages = [ cfg.package ];

            # Manage the plasmazonesd systemd user service via Home Manager.
            # Home Manager writes the service unit and `systemctl --user daemon-reload`
            # automatically during `home-manager switch`.
            systemd.user.services.plasmazones = {
              Unit = {
                Description = "PlasmaZones Window Tiling Daemon";
                # Start after the Wayland/graphical session is up.
                # graphical-session.target is set by KDE's plasma-session.
                PartOf = [ "graphical-session.target" ];
                After  = [ "graphical-session.target" ];
              };
              Service = {
                # plasmazonesd is the daemon binary installed to $out/bin.
                ExecStart  = "${cfg.package}/bin/plasmazonesd";
                # Restart on crash (not on clean exit — the user can stop it
                # intentionally via `systemctl --user stop plasmazones`).
                Restart    = "on-failure";
                RestartSec = 3;
              };
              # WantedBy graphical-session.target means `systemctl --user enable`
              # makes it start automatically with the KDE session.
              Install.WantedBy = [ "graphical-session.target" ];
            };
          };
        };

      # ── overlay ─────────────────────────────────────────────────────────────
      # Adds `pkgs.plasmazones` to a nixpkgs instance.
      # Use this if you manage nixpkgs yourself and want PlasmaZones as a
      # regular package attribute:
      #
      #   nixpkgs.overlays = [ inputs.plasmazones.overlays.default ];
      #   environment.systemPackages = [ pkgs.plasmazones ];
      #
      # `final` is the post-overlay pkgs — the host's current package set.
      # Using `final` (not `prev`) is what keeps kwin in sync with the host.
      overlays.default = final: prev: {
        plasmazones = mkPlasmaZones final;
      };

      # ── devShell ─────────────────────────────────────────────────────────────
      # A development environment with all build dependencies.
      # Enter with: nix develop
      #
      # From inside the shell you can build manually:
      #   cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
      #   cmake --build build -j$(nproc)
      #
      # Or run the test suite (requires a running Wayland compositor):
      #   cmake --build build --target test
      devShells = forAllSystems (system:
        let
          pkgs      = nixpkgs.legacyPackages.${system};
          # inputsFrom pulls in all nativeBuildInputs and buildInputs from
          # the package derivation, giving the shell everything needed to build.
          pz        = self.packages.${system}.default;
        in
        {
          default = pkgs.mkShell {
            # Inherit the full build environment from the package derivation.
            inputsFrom = [ pz ];

            # Additional developer tools not needed for the build itself:
            packages = with pkgs; [
              # Debugger — step through C++ code in the daemon or effect plugin.
              gdb

              # C++ language server and formatter — for editor integration.
              clang-tools   # provides clangd + clang-format

              # CMake LSP — useful with editors that support cmake-language-server.
              cmake-language-server

              # Qt-specific introspection tools.
              pkgs.qt6.qttools  # provides qdbus6, qmlformat, etc.
            ];

            # Tell CMake to use Debug mode in the dev shell.
            shellHook = ''
              echo "PlasmaZones dev shell ready."
              echo "Build:  cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && cmake --build build"
              echo "Test:   cmake --build build --target test  (requires running Wayland compositor)"
            '';
          };
        }
      );
    };
}

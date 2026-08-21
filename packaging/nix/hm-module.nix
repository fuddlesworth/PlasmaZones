# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/hm-module.nix — Home Manager module (programs.plasmazones).
#
# For per-user installs without NixOS (the Nix package manager on another
# distro, or dotfiles managed by Home Manager). The package default builds
# against the host's pkgs so the KWin effect plugin's IID matches the running
# KWin (see packaging/nix/overlays.nix).
#
# Usage:
#   inputs.plasmazones.url = "github:fuddlesworth/PlasmaZones";
#   { inputs, ... }: {
#     imports = [ inputs.plasmazones.homeManagerModules.default ];
#     programs.plasmazones.enable = true;
#   }
{ src, version }:

{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.programs.plasmazones;
in
{
  options.programs.plasmazones = {
    enable = lib.mkEnableOption "PlasmaZones window snapping, tiling and scrolling for KDE Plasma 6.7+";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { inherit src version; };
      defaultText = lib.literalExpression "pkgs.callPackage ./package.nix { inherit src version; }";
      description = ''
        The PlasmaZones package to use. Built against the host's pkgs so the
        KWin effect plugin IID matches the running KWin.
      '';
    };

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = ''
        Whether to start the PlasmaZones daemon automatically with the
        graphical session. Disable to install the unit without enabling it.

        Defaults to true here, unlike the NixOS module, which defaults to
        false. A Home Manager configuration already describes exactly one
        user, so enabling the unit affects only that user. The NixOS module
        would otherwise autostart the daemon for everyone on the machine.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    # Home Manager writes the unit and runs `systemctl --user daemon-reload`
    # during `home-manager switch`.
    systemd.user.services.plasmazones = {
      Unit = {
        Description = "PlasmaZones Window Placement Daemon";
        # Start after the Wayland/graphical session is up (set by plasma-session).
        PartOf = [ "graphical-session.target" ];
        After = [ "graphical-session.target" ];
      };
      Service = {
        ExecStart = "${cfg.package}/bin/plasmazonesd";
        # Restart on crash, not on a clean stop (the user can stop it manually).
        Restart = "on-failure";
        RestartSec = 3;
      };
      Install = lib.mkIf cfg.autostart {
        WantedBy = [ "graphical-session.target" ];
      };
    };

    # Refresh the KDE service cache after activation so the KCM (System
    # Settings -> Apps -> PlasmaZones) appears without a logout. The NixOS
    # module does this from system.userActivationScripts; without the same step
    # here, a Home Manager install on a non-NixOS host puts the KCM plugin and
    # its .desktop file in place but System Settings does not list them until
    # the user runs kbuildsycoca6 by hand. Best-effort: only fires where KDE is
    # installed, and never fails the activation.
    home.activation.plasmazones-sycoca = lib.hm.dag.entryAfter [ "writeBoundary" ] ''
      if command -v kbuildsycoca6 >/dev/null 2>&1; then
        $DRY_RUN_CMD kbuildsycoca6 --noincremental 2>/dev/null || true
      fi
    '';
  };
}

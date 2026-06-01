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
    enable = lib.mkEnableOption "PlasmaZones window tiling for KDE Plasma 6.6+";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { inherit src version; };
      defaultText = lib.literalExpression "pkgs.callPackage ./package.nix { }";
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
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    # Home Manager writes the unit and runs `systemctl --user daemon-reload`
    # during `home-manager switch`.
    systemd.user.services.plasmazones = {
      Unit = {
        Description = "PlasmaZones Window Tiling Daemon";
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
  };
}

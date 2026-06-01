# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/module.nix — NixOS module (programs.plasmazones).
#
# This is the recommended install path on NixOS: the package default builds
# against the host's pkgs, so the KWin effect plugin's IID matches the running
# KWin (see packaging/nix/overlays.nix for the full rationale).
#
# Usage:
#   inputs.plasmazones.url = "github:fuddlesworth/PlasmaZones";
#   { inputs, ... }: {
#     imports = [ inputs.plasmazones.nixosModules.default ];
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
        The PlasmaZones package to use. The default builds against the host
        system's pkgs so the KWin effect plugin's IID matches the running KWin
        binary. Override only if you know what you are doing.
      '';
    };

    autostart = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Whether to start the PlasmaZones daemon automatically with the KDE
        session. When false (the default) the systemd user unit is installed
        but inactive; start it per-user with:

          systemctl --user enable --now plasmazones

        PlasmaZones is a per-user tool, so autostart is opt-in rather than
        enabled for every user on the system.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    # Install system-wide. NixOS adds each system-profile package's
    # lib/qt-6/plugins to QT_PLUGIN_PATH, which is how the running KWin
    # discovers the bundled effect plugin (see package.nix QTPLUGINDIR note).
    environment.systemPackages = [ cfg.package ];

    # Declare the daemon as a per-user unit so it is reproducible and
    # GC-rooted, rather than relying solely on the copy shipped inside the
    # package. Autostart stays opt-in (see the `autostart` option).
    systemd.user.services.plasmazones = {
      description = "PlasmaZones Window Tiling Daemon";
      partOf = [ "graphical-session.target" ];
      after = [ "graphical-session.target" ];
      wantedBy = lib.mkIf cfg.autostart [ "graphical-session.target" ];
      serviceConfig = {
        ExecStart = "${cfg.package}/bin/plasmazonesd";
        Restart = "on-failure";
        RestartSec = 3;
      };
    };

    # Refresh the KDE service cache once at activation so the KCM (System
    # Settings -> Window Management -> PlasmaZones) appears without a logout.
    # Best-effort; only runs if KDE is installed.
    system.userActivationScripts.plasmazones-sycoca.text = ''
      if command -v kbuildsycoca6 >/dev/null 2>&1; then
        kbuildsycoca6 --noincremental 2>/dev/null || true
      fi
    '';
  };
}

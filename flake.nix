# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "Window tiling and autotiling for KDE Plasma 6.6+";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

      # Build PlasmaZones against a *caller-supplied* nixpkgs instance.
      #
      # The KWin effect plugin's IID embeds KWin's exact upstream version
      # string; KWin refuses to load any effect whose IID does not match the
      # running KWin — even across patch releases (6.6.4 -> 6.6.5). So the
      # package MUST be compiled against the very same `kwin` the user runs.
      #
      # For module/overlay consumers that means the *consumer's* `pkgs`, never
      # this flake's pinned `nixpkgs` input — otherwise a rolling system whose
      # KWin has moved past flake.lock gets a silently non-loading effect
      # (see discussion #481).
      mkPlasmaZones = pkgs: pkgs.callPackage ./packaging/nix/package.nix {
        src = self;
      };
    in
    {
      packages = forAllSystems (system:
        {
          # Built against this flake's pinned nixpkgs — fine for `nix build`
          # and CI checks, but NOT a reliable install path for the KWin effect
          # on a rolling system. Prefer the nixosModule / overlay below.
          default = mkPlasmaZones nixpkgs.legacyPackages.${system};
          plasmazones = self.packages.${system}.default;
        }
      );

      checks = forAllSystems (system: {
        # Build the package as a check — catches C++ compilation errors
        package = self.packages.${system}.default;
      });

      # NixOS module for systemd service integration
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.plasmazones;
        in
        {
          options.programs.plasmazones = {
            enable = lib.mkEnableOption "PlasmaZones window tiling for KDE Plasma 6.6+";
            package = lib.mkOption {
              type = lib.types.package;
              default = mkPlasmaZones pkgs;
              defaultText = lib.literalExpression
                "pkgs.callPackage \"\${plasmazones}/packaging/nix/package.nix\" { src = plasmazones; }";
              description = ''
                The PlasmaZones package to use. Built against the host's
                nixpkgs so the KWin effect plugin's IID matches the running
                KWin (see discussion #481).
              '';
            };
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ];

            # The systemd user service is installed by the package;
            # users enable it with: systemctl --user enable --now plasmazones.service
          };
        };

      # Home Manager module (for per-user installs without NixOS)
      homeManagerModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.plasmazones;
        in
        {
          options.programs.plasmazones = {
            enable = lib.mkEnableOption "PlasmaZones window tiling for KDE Plasma 6.6+";
            package = lib.mkOption {
              type = lib.types.package;
              default = mkPlasmaZones pkgs;
              defaultText = lib.literalExpression
                "pkgs.callPackage \"\${plasmazones}/packaging/nix/package.nix\" { src = plasmazones; }";
              description = ''
                The PlasmaZones package to use. Built against the host's
                nixpkgs so the KWin effect plugin's IID matches the running
                KWin (see discussion #481).
              '';
            };
          };

          config = lib.mkIf cfg.enable {
            home.packages = [ cfg.package ];
            systemd.user.services.plasmazones = {
              Unit = {
                Description = "PlasmaZones Window Snapping Daemon";
                PartOf = [ "graphical-session.target" ];
                After = [ "graphical-session.target" ];
              };
              Service = {
                ExecStart = "${cfg.package}/bin/plasmazonesd";
                Restart = "on-failure";
                RestartSec = 3;
              };
              Install.WantedBy = [ "graphical-session.target" ];
            };
          };
        };

      overlays.default = final: prev: {
        # Build against the consumer's pkgs (`final`) so the KWin effect is
        # compiled against their KWin — see mkPlasmaZones above.
        plasmazones = mkPlasmaZones final;
      };

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          plasmazones = self.packages.${system}.default;
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ plasmazones ];
            packages = with pkgs; [
              gdb
              clang-tools
            ];
          };
        }
      );
    };
}

# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "FancyZones-style window tiling for KDE Plasma 6";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.callPackage ./packaging/nix/package.nix {
            src = self;
          };
          plasmazones = self.packages.${system}.default;
        }
      );

      checks = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          # Build the package as a check — catches C++ compilation errors
          package = self.packages.${system}.default;

          # Validate Nix file formatting
          nix-fmt = pkgs.runCommandLocal "check-nix-fmt" {
            nativeBuildInputs = [ pkgs.nixfmt-rfc-style ];
            src = self;
          } ''
            nixfmt --check "$src"/flake.nix "$src"/packaging/nix/package.nix
            mkdir "$out"
          '';
        }
      );

      formatter = forAllSystems (system:
        nixpkgs.legacyPackages.${system}.nixfmt-rfc-style
      );

      # NixOS module for systemd service integration
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.plasmazones;
        in
        {
          options.programs.plasmazones = {
            enable = lib.mkEnableOption "PlasmaZones window tiling for KDE Plasma 6";
            package = lib.mkPackageOption pkgs "plasmazones" {
              default = self.packages.${pkgs.system}.default;
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
            enable = lib.mkEnableOption "PlasmaZones window tiling for KDE Plasma 6";
            package = lib.mkPackageOption pkgs "plasmazones" {
              default = self.packages.${pkgs.system}.default;
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
        plasmazones = self.packages.${prev.system}.default;
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
              nixfmt-rfc-style
            ];
          };
        }
      );
    };
}

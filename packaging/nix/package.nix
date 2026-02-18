# PlasmaZones Nix package
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Requires Plasma 6.6+ (KF6 6.6, Qt 6.6, LayerShellQt 6.6, KWin 6.6).
# Use nixpkgs with Plasma 6.6 or later (e.g. nixos-unstable or a branch that has
# KWin 6.6 effect API and LayerShellQt 6.6 setScreen API).
#
# Usage:
#   plasmazones = pkgs.callPackage ./packaging/nix/package.nix {
#     src = pkgs.fetchFromGitHub {
#       owner = "fuddlesworth";
#       repo = "PlasmaZones";
#       rev = "v<VERSION>";           # any tag or commit
#       hash = lib.fakeHash;          # build once, nix prints the real hash
#     };
#   };
#
# For pre-built release packages with hash included, download plasmazones.nix
# from the GitHub release assets.
{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  src,
  qt6,
  kdePackages,
}:

let
  # Extract the canonical version from CMakeLists.txt so there is exactly
  # one source of truth.  Nix's builtins.match operates on the whole string
  # and '.' does not match newlines, so we split into lines first, find the
  # project() line, then pull the version number out of it.
  extractVersion = src:
    let
      lines = lib.splitString "\n" (builtins.readFile (src + "/CMakeLists.txt"));
      projectLine = lib.findFirst
        (l: builtins.match ".*project\\(PlasmaZones VERSION.*" l != null)
        null
        lines;
      parsed =
        if projectLine != null
        then builtins.match ".*VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*" projectLine
        else null;
    in
    if parsed != null then builtins.head parsed else "0.0.0";
in

stdenv.mkDerivation {
  pname = "plasmazones";
  version = extractVersion src;

  inherit src;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
    kdePackages.extra-cmake-modules
  ];

  buildInputs = [
    # Qt 6 — explicit deps matching CMakeLists.txt find_package(Qt6 6.6 COMPONENTS …)
    qt6.qtbase           # Core Gui Widgets DBus Concurrent
    qt6.qtdeclarative    # Quick QuickControls2 (QML engine + runtime)
    qt6.qtshadertools    # ShaderTools ShaderToolsPrivate (zone overlay shaders)
  ] ++ (with kdePackages; [
    # KDE Frameworks 6 (6.6+ for Plasma 6.6)
    kconfig
    kconfigwidgets
    kcoreaddons
    kdbusaddons
    ki18n
    kcmutils
    kwindowsystem
    kglobalaccel
    knotifications
    kcolorscheme

    # Wayland overlay support (required; 6.6+ for setScreen API)
    layer-shell-qt

    # KWin integration (effect plugin; 6.6+ for prePaintWindow(RenderView)/paintWindow(Region) API)
    kwin

    # Optional: activity-based layout switching
    # plasma-activities is auto-detected by CMake; omit to disable
    plasma-activities
  ]);

  # KCM is a .so plugin loaded by systemsettings — wrapQtAppsHook only wraps
  # executables, so QML runtime deps must be propagated into the user
  # environment so systemsettings can resolve them (e.g. org.kde.kirigami).
  propagatedBuildInputs = with kdePackages; [
    kirigami              # QML runtime: org.kde.kirigami (Dialog, Card, Icon, etc.)
    qqc2-desktop-style    # QML runtime: org.kde.desktop (Qt Quick Controls 2 styling)
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_TESTING=OFF"
  ];

  # ECM's KDEInstallDirs sets KDE_INSTALL_SYSTEMDUSERUNITDIR as a normal
  # variable (shadowing any -D cache override) and may resolve it to an
  # absolute host path (e.g. /usr/lib/systemd/user) that falls outside $out
  # in the Nix sandbox.  Force the relative path after KDEInstallDirs runs.
  preConfigure = ''
    substituteInPlace CMakeLists.txt \
      --replace-fail \
        'if(NOT DEFINED KDE_INSTALL_SYSTEMDUSERUNITDIR)' \
        'if(TRUE)  # Force relative path for Nix — original: if(NOT DEFINED KDE_INSTALL_SYSTEMDUSERUNITDIR)'
  '';

  # The upstream systemd unit hardcodes /usr/bin; patch to the store path
  postInstall = ''
    substituteInPlace $out/lib/systemd/user/plasmazones.service \
      --replace-fail "/usr/bin/plasmazonesd" "$out/bin/plasmazonesd"
  '';

  meta = {
    description = "FancyZones-style window tiling for KDE Plasma 6.6+";
    homepage = "https://github.com/fuddlesworth/PlasmaZones";
    changelog = "https://github.com/fuddlesworth/PlasmaZones/blob/main/CHANGELOG.md";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
    mainProgram = "plasmazonesd";
  };
}

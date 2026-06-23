# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/nix/package.nix — PlasmaZones build recipe for Nix/nixpkgs
#
# This file is the *single source of truth* for how to build PlasmaZones
# under Nix. It is consumed by two callers:
#
#   1. flake.nix (in the repository root) — for `nix build` and `nix profile install`
#   2. nixpkgs (eventually) — once the package is submitted upstream
#
# HOW TO CALL THIS FILE
# ─────────────────────
# Always call it with the pkgs instance of the *host system*, never with the
# flake's own pinned nixpkgs. The KWin effect plugin embeds KWin's exact
# version string as its IID; KWin refuses to load any plugin whose IID does
# not match the running KWin binary — even across patch releases. Compiling
# against the wrong pkgs produces a plugin that silently fails to load.
#
#   # Correct (host pkgs — matches the running KWin):
#   pkgs.callPackage ./packaging/nix/package.nix { src = plasmazones-src; }
#
#   # Wrong (pinned pkgs — may not match the running KWin):
#   pinnedPkgs.callPackage ./packaging/nix/package.nix { ... }
#
# BUILD FLAGS
# ───────────
# We always pass -DUSE_KDE_FRAMEWORKS=ON. This enables:
#   • The KWin C++ effect plugin (kwin-effect/)
#   • The KDE System Settings module / KCM (kcm/)
#   • KGlobalAccel shortcut integration
# There is no reason to build without KDE frameworks on NixOS — the portable
# Qt-only mode exists for non-KDE compositors (Hyprland, Sway, GNOME). If
# you genuinely want the portable build, pass `withKdeFrameworks = false`.

{
  # -- Nixpkgs standard arguments injected by callPackage --
  lib,
  stdenv,
  fetchFromGitHub,       # Used when building from a pinned upstream release
  cmake,
  pkg-config,

  # -- Qt 6 --
  qt6,                   # The qt6 scope; we pull individual packages from it

  # -- Wayland --
  wayland,               # libwayland-client (runtime library linked into the binary)
  wayland-scanner,       # Code-gen tool: produces C bindings from Wayland protocol
                         # XMLs at build time (find_program(WAYLAND_SCANNER) in CMake)

  # -- KDE Frameworks 6 --
  kdePackages,           # The kdePackages scope for KF6 + Plasma

  # -- Vulkan headers (build-time, required by the daemon) --
  # The dma-buf thumbnail daemon path hard-requires the Vulkan SDK via
  # find_package(Vulkan REQUIRED) in src/CMakeLists.txt (dmabuftextureprovider.cpp
  # includes <vulkan/vulkan.h> unconditionally), so a daemon build without these
  # headers fails at configure time. (Qt's separate QVulkanInstance render backend
  # is still QT_CONFIG(vulkan)-guarded/optional — a distinct concern from this SDK.)
  vulkan-headers,

  # -- Caller-supplied source --
  # When used from flake.nix the src is a fileset-scoped view of the repo root.
  # When used from nixpkgs it will be a fetchFromGitHub derivation.
  src,

  # -- Caller-supplied version --
  # Parsed once from CMakeLists.txt in flake.nix, where the flake `self` is
  # always a store path so the read is pure. Reading it here instead would force
  # an import-from-derivation (IFD) when nixpkgs builds from a fetchFromGitHub
  # src — which nixpkgs forbids. nixpkgs callers pass a literal version string.
  version,

  # -- Feature flags (nixpkgs-style, override with .override { }) --
  withKdeFrameworks ? true,  # Set false for the portable Qt-only daemon+editor

  # LTO is OFF by default: every NixOS / Home-Manager / overlay consumer
  # rebuilds this package against their *host* pkgs (see overlays.nix), and LTO
  # adds 2-5× link time with no binary-cache reuse. Enable it only for the
  # cached release artifact via `.override { enableLTO = true; }`.
  enableLTO ? false,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "plasmazones";

  # Version is supplied by the caller (parsed from CMakeLists.txt in flake.nix,
  # the single source of truth — see the `version` argument above).
  inherit version src;

  # ── Build inputs ──────────────────────────────────────────────────────────
  #
  # nativeBuildInputs: tools that run on the *build* machine during compilation
  #                    (cmake, pkg-config, code generators, Qt MOC/RCC/UIC).
  # buildInputs:       libraries that are linked into the final binary and
  #                    therefore need to match the *host* architecture.

  nativeBuildInputs = [
    cmake
    pkg-config
    kdePackages.extra-cmake-modules  # ECM: KDEInstallDirs, KDECMakeSettings, etc. (Qt6 only — the top-level alias was removed)
    wayland-scanner       # Code-gen tool; must be the build-machine binary

    # Qt 6 build tools — MOC (meta-object compiler), RCC (resource compiler),
    # UIC (UI compiler), and the shader/QML tooling. These are always native
    # because they produce architecture-independent generated sources.
    qt6.qttools           # lupdate, lrelease, qmlformat, qdbus6
    qt6.wrapQtAppsHook    # Wraps installed binaries so they find Qt plugins
                          # (QT_PLUGIN_PATH, QML2_IMPORT_PATH, etc.) at runtime
                          # without manual environment variable management.
  ]
  # KDE Frameworks bring their own CMake modules and code generators
  # (kconfig_add_kcfg_files, etc.) that must run on the build machine.
  ++ lib.optionals withKdeFrameworks [
    kdePackages.kconfig   # Provides KConfigXT code generator used by the KCM
  ];

  buildInputs = [
    # ── Qt 6 runtime libraries ──────────────────────────────────────────────
    qt6.qtbase            # Core, Gui, Widgets, DBus — the Qt foundation
    qt6.qtdeclarative     # Qml, Quick, QuickControls2 — QML engine + runtime
    qt6.qtshadertools     # ShaderTools + ShaderToolsPrivate (QShaderBaker)
                          # Required for GPU shader compilation in the daemon
    qt6.qtsvg             # Svg module — used by the settings app and editor
    qt6.qtwayland         # WaylandClient + WaylandClientPrivate
                          # Direct Wayland protocol access (layer-shell overlays)

    # ── Wayland protocol library ─────────────────────────────────────────────
    # The CMakeLists does find_package(Wayland 1.21 REQUIRED COMPONENTS Client)
    # for the zwlr_layer_shell_v1 protocol implementation.
    wayland               # libwayland-client

    # ── Vulkan headers (build-time, required by the daemon) ───────────────────
    # Hard build requirement: the dma-buf thumbnail daemon path uses
    # find_package(Vulkan REQUIRED) (src/CMakeLists.txt) and includes
    # <vulkan/vulkan.h> unconditionally, so the daemon won't configure without
    # these. Only headers are needed — no runtime lib.
    vulkan-headers
  ]
  ++ lib.optionals withKdeFrameworks [
    # ── KDE Frameworks 6 ─────────────────────────────────────────────────────
    kdePackages.kconfig           # KConfig: persistent settings storage
    kdePackages.kcoreaddons       # KCoreAddons: KPluginFactory, KAboutData, etc.
    kdePackages.kcmutils          # KCMUtils: KDE System Settings module support
    kdePackages.kglobalaccel      # KGlobalAccel: system-wide keyboard shortcuts

    # ── KWin (for the C++ effect plugin) ─────────────────────────────────────
    # The kwin-effect/ subdirectory compiles a plugin that is loaded directly
    # by KWin. KWin exposes a private Effects API that the plugin links against.
    # THIS IS THE CRITICAL DEPENDENCY: the plugin's IID embeds the exact KWin
    # version. If this kwin package doesn't match the running KWin, the plugin
    # will be silently rejected at runtime. See the comment at the top of this
    # file and the flake.nix header for the full explanation.
    kdePackages.kwin

    # ── Optional KDE extras ───────────────────────────────────────────────────
    # PlasmaActivities enables per-activity zone layouts. The CMakeLists
    # searches for it with QUIET so the build succeeds without it.
    kdePackages.plasma-activities  # was: kactivities in KF5
  ];

  # ── CMake configuration flags ─────────────────────────────────────────────
  cmakeFlags = [
    # Always build Release — we never want debug symbols or assertions in a
    # production Nix package. (The devShell in flake.nix handles debug builds.)
    "-DCMAKE_BUILD_TYPE=Release"

    # KDE Frameworks integration: ON = full build with KWin effect + KCM + shortcuts.
    # This is what we always want on NixOS/KDE Plasma. The flag name comes
    # directly from the CMakeLists option(USE_KDE_FRAMEWORKS ...).
    "-DUSE_KDE_FRAMEWORKS=${if withKdeFrameworks then "ON" else "OFF"}"

    # Always build the KWin effect when KDE Frameworks are enabled.
    # The effect is the visual overlay drawn by KWin during window drags.
    "-DBUILD_KWIN_EFFECT=${if withKdeFrameworks then "ON" else "OFF"}"

    # Disable the test suite during package builds. Tests require a running
    # Wayland compositor and cannot pass in the Nix sandbox. They can be run
    # manually in the devShell with `cmake -DBUILD_TESTING=ON`.
    "-DBUILD_TESTING=OFF"

    # Disable developer tools (shader-render CLI, etc.) — not needed in packages.
    "-DBUILD_TOOLS=OFF"

    # Disable the phosphor-shell WIP desktop shell — not packaged yet.
    "-DBUILD_PHOSPHOR_SHELL=OFF"

    # LTO (Link-Time Optimization): opt-in, default OFF (see `enableLTO` above).
    # In Nix the toolchain is consistent so LTO is safe, but it is expensive for
    # the host-pkgs rebuild that every module/overlay consumer performs.
    "-DENABLE_LTO=${if enableLTO then "ON" else "OFF"}"

    # Install Qt/KWin plugins into *this package's* $out — never into another
    # derivation's store path. The previous value here was
    #   ${qt6.qtbase}/lib/qt6/plugins
    # an absolute path into the read-only qtbase output. A derivation may only
    # write under its own $out, so the KWin effect silently never landed in the
    # closure (the daemon ran, but zone overlays never appeared).
    #
    # `qtbase.qtPluginPrefix` is the relative "lib/qt-6/plugins" path that every
    # Qt 6 package on NixOS uses. The effect therefore installs to:
    #   $out/lib/qt-6/plugins/kwin/effects/plugins/kwin_effect_plasmazones.so
    # The running KWin discovers it because NixOS adds each system-profile
    # package's lib/qt-6/plugins to QT_PLUGIN_PATH (the package is placed in
    # environment.systemPackages by the NixOS module). This is the canonical
    # NixOS plugin layout — the same approach quickshell uses for its QML/plugins.
    "-DKDE_INSTALL_QTPLUGINDIR=${placeholder "out"}/${qt6.qtbase.qtPluginPrefix}"
  ];

  # ── Post-install fixups ───────────────────────────────────────────────────
  #
  # wrapQtAppsHook (added via nativeBuildInputs above) automatically wraps
  # every executable under $out/bin so that:
  #   • QT_PLUGIN_PATH includes the Qt plugin directories
  #   • QML2_IMPORT_PATH / QML_IMPORT_PATH includes the QML module directories
  #   • XDG_DATA_DIRS includes $out/share so KDE service discovery works
  #
  # For KDE specifically, the KCM (System Settings module) also needs
  # kbuildsycoca6 to be run *after install* on the user's machine to register
  # the new .desktop service file. This is a runtime step, not a build step —
  # the NixOS module / Home Manager module in flake.nix handles it via a
  # `system.userActivationScripts` entry or post-activation hook.

  # ── Metadata ──────────────────────────────────────────────────────────────
  meta = with lib; {
    description = "FancyZones-style window tiling and autotiling for KDE Plasma";
    longDescription = ''
      PlasmaZones brings Windows PowerToys FancyZones-style zone management
      to KDE Plasma. Define zones on your screen, then drag windows into them
      to snap and resize. Supports autotiling with 24 algorithms, a visual
      layout editor, GLSL shader effects on zone overlays, per-monitor and
      per-virtual-desktop layouts, and full KDE integration via a KWin effect
      plugin and System Settings module.

      The daemon (plasmazonesd) runs as a systemd user service and communicates
      over D-Bus. The settings app (plasmazones-settings) and layout editor are
      standalone Qt applications.
    '';
    homepage = "https://github.com/fuddlesworth/PlasmaZones";
    license = licenses.gpl3Plus;
    maintainers = [ ];   # Add your nixpkgs handle here when submitting upstream
    platforms = [ "x86_64-linux" "aarch64-linux" ];

    # PlasmaZones is Wayland-only. Plasma 6 dropped X11 as a session target
    # and the layer-shell overlays used for zone rendering have no X11 equivalent.
    # The CMakeLists explicitly states "Wayland-only" in comments.
  };
})

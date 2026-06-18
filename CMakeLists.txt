# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones - Window tiling and autotiling for KDE Plasma

cmake_minimum_required(VERSION 3.16)

project(PlasmaZones VERSION 3.0.16 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Build Performance Optimizations
# ═══════════════════════════════════════════════════════════════════════════════

# Unity builds - combine multiple source files into larger translation units
# This significantly reduces compilation time by reducing header parsing overhead
option(CMAKE_UNITY_BUILD "Enable unity builds for faster compilation" ON)
set(CMAKE_UNITY_BUILD_BATCH_SIZE 16 CACHE STRING "Number of sources per unity batch")

# Precompiled headers - reuse expensive header compilation
option(ENABLE_PCH "Enable precompiled headers" ON)

# ccache support - cache compilation results for faster rebuilds
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM} - enabling compiler cache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Parallel AUTOMOC for faster meta-object compilation
set(CMAKE_AUTOMOC_PARALLEL_ENABLED ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Release Build Optimizations (LTO and aggressive optimization)
# ═══════════════════════════════════════════════════════════════════════════════

# Link-Time Optimization (LTO) for Release builds
# Benefits: 5-15% runtime performance improvement, smaller binary size
# Trade-off: Significantly increased link times (2-5x longer) and memory usage
# Recommendation: Keep enabled for release builds, disable for development
# To disable: cmake -DENABLE_LTO=OFF ..
option(ENABLE_LTO "Enable Link-Time Optimization for Release builds" ON)
if(ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
    if(IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Link-Time Optimization (LTO) enabled for Release builds")
        message(STATUS "  Note: LTO increases link times significantly. Use -DENABLE_LTO=OFF for faster dev builds.")
    else()
        message(WARNING "LTO not supported by compiler: ${IPO_OUTPUT}")
    endif()
endif()

# QML compilation to C++ (qmlcachegen) for faster QML execution
# Qt6 enables this by default when using qt_add_qml_module
set(QT_QML_GENERATE_QMLLS_INI ON CACHE BOOL "Generate QML language server config")

# AUTOMOC's `<target>_autogen/include` path is cleaned by `--clean-first`
# and then recreated mid-build by the autogen step, so `-Wmissing-include-dirs`
# intermittently fires against these directories. Instead of racing the
# clean/autogen ordering from here at configure time (which `--clean-first`
# would wipe again), the suppression is scoped to src/shared/CMakeLists.txt
# where the affected QML-module targets live - see that file for details.

# KDE/Qt requirements (Plasma 6.7 target: KF 6.26, Qt 6.10, KWin 6.7)
set(QT_MIN_VERSION "6.10.0")
set(KF_MIN_VERSION "6.26.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# Test/tool toggles MUST be set before KDECMakeSettings, which calls
# include(CTest) and locks BUILD_TESTING into the cache as ON. Declaring
# them after KDECMakeSettings runs makes the option() call a no-op.
option(BUILD_TESTING "Build the test suite" OFF)
option(BUILD_TOOLS "Build developer tools (shader-render, etc.)" OFF)

include(KDEInstallDirs)
set(KDE_SKIP_UNINSTALL_TARGET ON)  # We provide our own uninstall target
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Find Qt6
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Concurrent
    Core
    Qml
    Quick
    QuickControls2
    Gui
    DBus
    Widgets
    ShaderTools
    Svg
)
# GuiPrivate (QRhi) and ShaderToolsPrivate (QShaderBaker) required for daemon zone overlay.
# Some distros (Fedora) create these targets inside Qt6GuiConfig.cmake rather than providing
# standalone config files, so only call find_package if the target doesn't already exist.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate CONFIG REQUIRED)
endif()
if(NOT TARGET Qt6::ShaderToolsPrivate)
    find_package(Qt6ShaderToolsPrivate CONFIG REQUIRED)
endif()
# QuickPrivate is required by phosphor-animation's QML module - must be
# discovered at top-level so the IMPORTED target is global and visible to
# every consumer of `PhosphorAnimation::PhosphorAnimation`. Without this,
# Qt's static-QML-plugin dependency walker emits "QuickPrivate target is
# mentioned as a dependency ... but not declared" when collecting
# transitive deps from the `src/shared` static-plugin consumer.
if(NOT TARGET Qt6::QuickPrivate)
    find_package(Qt6QuickPrivate CONFIG REQUIRED)
endif()

# Optional: Qt6::ShaderTools for shader effects support
# RenderNode shaders use raw GLSL - no Qt6::ShaderTools compilation needed
# Shaders are always enabled with the RenderNode implementation
message(STATUS "RenderNode shaders ENABLED (raw GLSL, no qsb compilation)")
set(PLASMAZONES_SHADERS_ENABLED ON)
add_compile_definitions(PLASMAZONES_SHADERS_ENABLED)

# KDE Frameworks integration.
# ON (default): full KDE integration - KWin effect, KCM, KGlobalAccel shortcuts,
#               PlasmaActivities, KConfigXT for KCM defaults.
# OFF: portable Qt-only build - daemon + editor only, no KDE dependencies.
#      Shortcuts use XDG Portal or D-Bus trigger fallback.
option(USE_KDE_FRAMEWORKS "Build with KDE Frameworks integration" ON)

# Opt-in build for the in-progress phosphor-shell desktop shell and its
# supporting libraries (libs/phosphor-shell, libs/phosphor-services) +
# the bundled `phosphor-shell` example binary + the example QML/shader
# tree under examples/phosphor-shell. Default OFF so packaging /
# downstream installs of PlasmaZones don't pick up shell artefacts
# before the shell ships. Enable explicitly for shell development:
#   cmake -DBUILD_PHOSPHOR_SHELL=ON
option(BUILD_PHOSPHOR_SHELL "Build the phosphor-shell desktop shell (work in progress)" OFF)

if(USE_KDE_FRAMEWORKS)
    add_compile_definitions(USE_KDE_FRAMEWORKS)

    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
        KCMUtils
        GlobalAccel   # KGlobalAccel shortcut backend + KCM
    )

    # KActivities/PlasmaActivities is optional (for activity-based layouts)
    find_package(PlasmaActivities QUIET)
    if(NOT PlasmaActivities_FOUND)
        find_package(KF6Activities QUIET COMPONENTS Activities)
        if(KF6Activities_FOUND)
            set(PlasmaActivities_FOUND TRUE)
        endif()
    endif()
    if(NOT PlasmaActivities_FOUND)
        find_package(KActivities QUIET)
        if(KActivities_FOUND)
            set(PlasmaActivities_FOUND TRUE)
        endif()
    endif()

    if(PlasmaActivities_FOUND)
        message(STATUS "PlasmaActivities found - activity-based layouts enabled")
    else()
        message(STATUS "PlasmaActivities/KActivities not found - activity support disabled")
    endif()
else()
    message(STATUS "Building WITHOUT KDE Frameworks (portable Qt-only mode)")
endif()

# Wayland layer-shell support (direct zwlr_layer_shell_v1 protocol, no KDE
# dependency). PlasmaZones is Wayland-only — Plasma 6 dropped X11 as a
# session target, and the layer-shell overlays we use have no X equivalent.
find_package(Wayland 1.21 REQUIRED COMPONENTS Client)
find_package(Qt6 6.10 REQUIRED COMPONENTS WaylandClient)
if(NOT TARGET Qt6::WaylandClientPrivate)
    find_package(Qt6WaylandClientPrivate REQUIRED)
endif()
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Systemd user unit directory (fallback if not provided by ECM). Defer to
# GNUInstallDirs' libdir so multilib distros land on the right path
# (e.g. lib64/systemd/user on Fedora, lib/x86_64-linux-gnu/systemd/user
# on Debian multiarch).
if(NOT DEFINED KDE_INSTALL_SYSTEMDUSERUNITDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_SYSTEMDUSERUNITDIR "${CMAKE_INSTALL_LIBDIR}/systemd/user")
endif()

# ---------------------------------------------------------------------------
# Code formatting (clang-format for C++, qmlformat for QML)
#   make clang-format  (or ninja clang-format)
#   make format-qml   (or ninja format-qml)
# ---------------------------------------------------------------------------
include(KDEClangFormat)
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.cpp
    ${CMAKE_SOURCE_DIR}/src/**/*.h
    ${CMAKE_SOURCE_DIR}/src/**/*.hpp
    ${CMAKE_SOURCE_DIR}/kcm/*.cpp
    ${CMAKE_SOURCE_DIR}/kcm/*.h
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.cpp
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.h
    ${CMAKE_SOURCE_DIR}/tests/**/*.cpp
    ${CMAKE_SOURCE_DIR}/tests/**/*.h
)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

# QML: qmlformat (from qt6-tools, optional)
find_program(QMLFORMAT qmlformat)
file(GLOB_RECURSE ALL_QML_FILES
    ${CMAKE_SOURCE_DIR}/src/**/*.qml
    ${CMAKE_SOURCE_DIR}/kcm/**/*.qml
)
if(QMLFORMAT AND ALL_QML_FILES)
    # Run qmlformat via helper script; per-file failures are ignored (one bad QML won't fail target)
    add_custom_target(format-qml
        COMMAND ${CMAKE_COMMAND}
            -DQMLFORMAT=${QMLFORMAT}
            -DFILES="${ALL_QML_FILES}"
            -P ${CMAKE_SOURCE_DIR}/cmake/format-qml.cmake
        COMMENT "Formatting QML files with qmlformat"
    )
else()
    add_custom_target(format-qml
        COMMAND ${CMAKE_COMMAND} -E echo "qmlformat not found or no QML files; install qt6-tools for QML formatting"
    )
endif()

# ---------------------------------------------------------------------------
# Translations (Qt Linguist)
#
# Daemon and editor use Qt Linguist (.ts/.qm).  lupdate scans source files
# directly - i18n()/i18nc() are backed by Q_DECLARE_TR_FUNCTIONS(plasmazones)
# in pz_i18n.h, so lupdate recognizes them as tr() calls.
#
#   make update-ts   - run lupdate to refresh .ts from source
#   make (default)   - compiles translations/*/*.ts → .qm
# ---------------------------------------------------------------------------
find_package(Qt6LinguistTools QUIET)

# Source files for lupdate string extraction (daemon + autotile + editor)
set(PLASMAZONESD_I18N_SOURCES
    ${CMAKE_SOURCE_DIR}/src/daemon/main.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/daemon/osd.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/overlayservice.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/shortcutmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/windowdragadaptor.cpp
    ${CMAKE_SOURCE_DIR}/src/dbus/windowdragadaptor/drag.cpp
)
file(GLOB PLASMAZONESD_AUTOTILE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/autotile/algorithms/*.cpp"
)
list(APPEND PLASMAZONESD_I18N_SOURCES ${PLASMAZONESD_AUTOTILE_SOURCES})
file(GLOB PLASMAZONESD_QML "${CMAKE_SOURCE_DIR}/src/ui/*.qml")

file(GLOB PLASMAZONES_EDITOR_I18N_SOURCES
    "${CMAKE_SOURCE_DIR}/src/editor/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/EditorController.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/gaps.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/shader.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/layout.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/selection.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/multiselect.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/visibility.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/clipboard.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/zones.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/zoneops.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/controller/settings.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/undo/commands/*.cpp"
)
file(GLOB PLASMAZONES_EDITOR_QML "${CMAKE_SOURCE_DIR}/src/editor/qml/*.qml")

# KCM QML sources (also covered by lupdate now)
file(GLOB_RECURSE KCM_PLASMAZONES_QML "${CMAKE_SOURCE_DIR}/kcm/*.qml")
list(APPEND KCM_PLASMAZONES_QML ${CMAKE_SOURCE_DIR}/src/shared/AspectRatioBadge.qml)
list(APPEND KCM_PLASMAZONES_QML ${CMAKE_SOURCE_DIR}/src/shared/CategoryBadge.qml)

# All sources for lupdate (daemon + editor + KCM - same translation context)
set(_all_i18n_sources
    ${PLASMAZONESD_I18N_SOURCES} ${PLASMAZONESD_QML}
    ${PLASMAZONES_EDITOR_I18N_SOURCES} ${PLASMAZONES_EDITOR_QML}
    ${KCM_PLASMAZONES_QML}
)

# Collect per-language .ts files (plasmazones_de.ts, plasmazones_fr.ts, etc.)
# Flat layout: translations/plasmazones_<lang>.ts → plasmazones_<lang>.qm
file(GLOB TRANSLATION_TS_FILES "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")
# Exclude the English source template from compilation (it has no translations)
list(FILTER TRANSLATION_TS_FILES EXCLUDE REGEX "plasmazones_en\\.ts$")

# --- update-ts target: lupdate scans source directly ---
# PzI18n::tr() uses Q_DECLARE_TR_FUNCTIONS(plasmazones) - lupdate
# recognizes tr() natively.  No custom scripts or intermediate files.
if(Qt6LinguistTools_FOUND)
    # All .ts files to update (en template + per-language)
    file(GLOB _all_ts_files "${CMAKE_SOURCE_DIR}/translations/plasmazones_*.ts")

    add_custom_target(update-ts
        COMMENT "Updating .ts translation files from source (lupdate)"
        COMMAND Qt6::lupdate
            -I ${CMAKE_SOURCE_DIR}/src
            ${_all_i18n_sources}
            -ts ${_all_ts_files}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
    add_custom_target(update-ts
        COMMAND ${CMAKE_COMMAND} -E echo "Qt6LinguistTools not found; install qt6-tools-dev"
    )
endif()

# --- Compile .ts → .qm and install ---
# Output: plasmazones_de.qm, plasmazones_fr.qm, etc.
# QTranslator::load(locale, "plasmazones", "_", dir) finds these by name.
if(Qt6LinguistTools_FOUND AND TRANSLATION_TS_FILES)
    qt_add_lrelease(plasmazones_translations
        TS_FILES ${TRANSLATION_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
    install(FILES ${QM_FILES} DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/translations)
endif()


# Generate version header with compile-time version info
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)
include_directories("${CMAKE_BINARY_DIR}/generated")

# Phosphor* libraries live under libs/ to keep the top-level tree tidy.
# Each ships its own CMakeLists, project() metadata, and find_package
# config so they stay individually extractable via `git subtree split`
# when they graduate to standalone repos. Add new libraries to libs/
# following the same phosphor-<name> convention.
add_subdirectory(libs/phosphor-identity)    # stable cross-process window identity (composite ids, app-id matching)
add_subdirectory(libs/phosphor-dbus)        # generic, service-agnostic D-Bus client utilities (Client, HasDBusStreaming)
add_subdirectory(libs/phosphor-protocol)    # D-Bus wire types and service constants — needed by phosphor-screens (Resolver endpoint defaults)
add_subdirectory(libs/phosphor-layout-api)  # shared layout-preview contract (LayoutPreview, ILayoutSource)
add_subdirectory(libs/phosphor-fsloader)    # filesystem-backed loader scaffolding (WatchedDirectorySet + DirectoryLoader)
add_subdirectory(libs/phosphor-config)      # schema-driven configuration + migration runner - needed by phosphor-zones
add_subdirectory(libs/phosphor-shaders)     # shader-domain primitives (BaseUniforms, IUniformExtension, ShaderRegistry)
add_subdirectory(libs/phosphor-wayland)       # Wayland layer-shell QPA plugin + LayerSurface - needed by phosphor-screens
add_subdirectory(libs/phosphor-screens)     # screen-topology + virtual-screen subdivision - needed by phosphor-zones
add_subdirectory(libs/phosphor-geometry)    # pure geometry math (coordinate transforms, overlap, min-size)
add_subdirectory(libs/phosphor-zones)       # zone primitives + LayoutManager (depends on phosphor-config IBackend, phosphor-screens ScreenIdentity)
add_subdirectory(libs/phosphor-tiles)       # auto-tiling algorithm primitives (TilingAlgorithm, AlgorithmRegistry, SplitTree)
add_subdirectory(libs/phosphor-rendering)   # GPU shader rendering via Qt RHI
add_subdirectory(libs/phosphor-layer)       # wlr-layer-shell surface lifecycle management
add_subdirectory(libs/phosphor-shell-patterns) # UI-pattern recipes (axis 2) built on PhosphorLayer::Role
add_subdirectory(libs/phosphor-shortcuts)   # global shortcut backends + domain-free Registry
# PlasmaZones consumes the org.phosphor.animation QML module for shell
# UI animations. PHOSPHOR_ANIMATION_QUICK is enabled before
# add_subdirectory picks up the option() default so QtQuickClock builds.
set(PHOSPHOR_ANIMATION_QUICK ON CACHE BOOL "Build QtQuickClock (required by QML animation module)")
add_subdirectory(libs/phosphor-animation)   # unified animation library: motion runtime, QML module, shaders, layer
add_subdirectory(libs/phosphor-audio)       # audio spectrum provider (CAVA backend)
add_subdirectory(libs/phosphor-surfaces)    # managed surface lifecycle (engine, keep-alive, scope gen)
add_subdirectory(libs/phosphor-overlay)     # per-screen shell-host + slot mechanism (rides on layer/shell-patterns/surfaces/animation/screens)
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(libs/phosphor-shell)   # shell infrastructure and QML types (BUILD_PHOSPHOR_SHELL gate)
endif()
add_subdirectory(libs/phosphor-engine)  # unified placement engine contracts (INTERFACE)
add_subdirectory(libs/phosphor-workspaces)  # virtual desktop / workspace management
add_subdirectory(libs/phosphor-tile-engine) # autotile placement engine (IAutotileSettings + engine code)
add_subdirectory(libs/phosphor-snap-engine) # zone-based snap placement engine

# ─── Engine QML firewall ─────────────────────────────────────────────────────
#
# Engine libraries hold authoritative placement/snap/zone state and must
# stay free of the QML/JS engine. The moment one of them links Qt6::Qml or
# Qt6::Quick, the cost model changes: every QObject in the engine gets a
# JS scope, bindings cross language boundaries, and ownership becomes
# implicit through QQmlEngine. That's the trajectory Noctalia couldn't
# escape — their engines and config were both QML, so multi-day uptime
# leaks (FD exhaustion, GC pauses, JSValue retention across reloads) had
# nowhere to be fixed without a full rewrite.
#
# Refuse to configure if any engine target ever picks up a Qml/Quick
# link. Cheap guardrail; failures here mean someone added a dependency
# that drags Qml in transitively or wired a target up wrong.
foreach(_pz_engine_lib PhosphorZones PhosphorSnapEngine PhosphorTileEngine PhosphorEngine)
    if(TARGET ${_pz_engine_lib})
        get_target_property(_pz_engine_links ${_pz_engine_lib} LINK_LIBRARIES)
        if(_pz_engine_links)
            foreach(_pz_link IN LISTS _pz_engine_links)
                if(_pz_link MATCHES "(^|::)Qt6::(Qml|Quick)$")
                    message(FATAL_ERROR
                        "Engine library '${_pz_engine_lib}' links '${_pz_link}'. "
                        "Engine libs must stay QML-free — see comment above this "
                        "check in CMakeLists.txt for rationale.")
                endif()
            endforeach()
        endif()
        get_target_property(_pz_engine_iface ${_pz_engine_lib} INTERFACE_LINK_LIBRARIES)
        if(_pz_engine_iface)
            foreach(_pz_link IN LISTS _pz_engine_iface)
                if(_pz_link MATCHES "(^|::)Qt6::(Qml|Quick)$")
                    message(FATAL_ERROR
                        "Engine library '${_pz_engine_lib}' publicly links '${_pz_link}'. "
                        "Engine libs must stay QML-free — see comment above this "
                        "check in CMakeLists.txt for rationale.")
                endif()
            endforeach()
        endif()
    endif()
endforeach()

add_subdirectory(libs/phosphor-placement)   # window placement state tracking
add_subdirectory(libs/phosphor-compositor)  # compositor-plugin SDK
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(libs/phosphor-services) # DBus + platform-integration primitives (SNI tray, dbusmenu, future: notifications/MPRIS/UPower)
endif()

# Source directories
add_subdirectory(src)

# Developer tools - opt-in via -DBUILD_TOOLS=ON.  Currently builds
# plasmazones-shader-render (offscreen shader preview generator
# used by the docs site).  Skipped by default so packagers don't
# pick up CLI tools they didn't ask for.
if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

option(BUILD_KWIN_EFFECT "Build KWin C++ effect plugin (needs KWin 6.7+)" ON)
if(USE_KDE_FRAMEWORKS)
    if(BUILD_KWIN_EFFECT)
        add_subdirectory(kwin-effect)  # KWin C++ Effect (KDE-only)
    endif()
    add_subdirectory(kcm)          # KDE System Settings modules (KDE-only)
endif()

# Tests - enable with -DBUILD_TESTING=ON. Option is declared at the top
# of the file (before KDECMakeSettings) so the OFF default actually wins;
# this block just gates the test subdirectory.
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install D-Bus interface XMLs (one per interface). Keep in sync with
# dbus/*.xml on disk — third-party clients (and qdbus-qt6 introspection)
# resolve interfaces against these files post-install.
install(FILES
    dbus/org.plasmazones.Autotile.xml
    dbus/org.plasmazones.CompositorBridge.xml
    dbus/org.plasmazones.Control.xml
    dbus/org.plasmazones.EditorApp.xml
    dbus/org.plasmazones.LayoutRegistry.xml
    dbus/org.plasmazones.Overlay.xml
    dbus/org.plasmazones.Screen.xml
    dbus/org.plasmazones.Settings.xml
    dbus/org.plasmazones.SettingsApp.xml
    dbus/org.plasmazones.Shader.xml
    dbus/org.plasmazones.WindowDrag.xml
    dbus/org.plasmazones.WindowTracking.xml
    dbus/org.plasmazones.ZoneDetection.xml
    DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)

# Install default layouts
install(DIRECTORY data/layouts/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/layouts
    FILES_MATCHING PATTERN "*.json"
)

# Settings embeds whatsnew.json as a Qt resource at build time. Also
# install it on disk so docs/release tooling can read the user-facing
# entries without grabbing the binary's resource bundle.
install(FILES data/whatsnew.json
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones
)

# Install shared named curves. Profile JSONs reference these by name
# (e.g. `"curve": "widget-out"`) so a single edit to a curve file
# retunes every profile that uses it, and users can drop their own
# JSON under ~/.local/share/plasmazones/curves/ to override a shipped
# curve system-wide. The daemon's CurveLoader scans this path before
# ProfileLoader runs.
install(DIRECTORY data/curves/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/curves
    FILES_MATCHING PATTERN "*.json"
)

# Defensive cleanup: an upgrade from a build that shipped per-leaf JSONs
# (every commit before the family-seed migration) leaves stale files in
# the install dir that `cmake --install` does NOT remove on its own -
# install only adds/updates, never deletes orphans. Those stale leaves
# silently shadow the user's parent-node Settings edits via the
# deeper-leaf-wins overlay walk in PhosphorProfileRegistry::resolveWithInheritance,
# making "All Popups → 2000 ms" appear to do nothing. Wipe the install
# dir BEFORE the install(DIRECTORY) below copies new content so the
# post-install state always matches the source tree.
#
# SCRIPT runs at install time (`cmake --install`), not configure time,
# and runs in the install prefix the user actually invoked install with
# (DESTDIR-aware via $ENV{DESTDIR}). The wipe is bounded to our own
# `plasmazones/profiles` subdirectory - never to a directory we don't
# own - so an accidental wide install prefix can't cascade.
# Use KDE_INSTALL_FULL_DATADIR (already prefix-resolved by ECM) so we
# don't manually concatenate CMAKE_INSTALL_PREFIX with KDE_INSTALL_DATADIR
# — that join produces `/usr//absolute/...` when ECM hands us an absolute
# KDE_INSTALL_DATADIR (some distros override it that way).
install(CODE "
    set(_pz_profiles_install_dir \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATADIR}/plasmazones/profiles\")
    if(EXISTS \"\${_pz_profiles_install_dir}\")
        file(GLOB _pz_stale_profile_jsons \"\${_pz_profiles_install_dir}/*.json\")
        if(_pz_stale_profile_jsons)
            message(STATUS \"Removing stale animation profile JSONs from \${_pz_profiles_install_dir}\")
            file(REMOVE \${_pz_stale_profile_jsons})
        endif()
    endif()
")

# The data/profiles/ directory no longer ships per-leaf JSONs - animation
# timings/curves are driven entirely from the Settings UI via
# PhosphorProfileRegistry, with parent-chain inheritance filling in
# unset leaves. The install(DIRECTORY) rule and the build-time
# check-animation-profiles target were removed because there are no
# profile JSONs to install or validate. The install(CODE) stale-cleanup
# block above is intentionally kept for upgrades from older builds.

# Install bundled RenderNode shaders (raw GLSL, no compilation needed)
install(DIRECTORY data/shaders/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/shaders
    FILES_MATCHING
    PATTERN "metadata.json"
    PATTERN "*.glsl"
    PATTERN "*.vert"
    PATTERN "*.frag"
    PATTERN "*.png"
    PATTERN "*.svg"
)

# Install bundled animation transition shader packs. The daemon's
# AnimationShaderRegistry scans ${XDG_DATA_DIRS}/plasmazones/animations
# at startup; user overrides at ~/.local/share/plasmazones/animations win.
install(DIRECTORY data/animations/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/animations
    FILES_MATCHING
    PATTERN "metadata.json"
    PATTERN "*.frag"
    PATTERN "*.vert"
    # GLSL include files (shared/animation_uniforms.glsl, shared/animation.vert).
    PATTERN "*.glsl"
    PATTERN "*.png"
)

# Install bundled scripted tiling algorithms
install(DIRECTORY data/algorithms/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/algorithms
    FILES_MATCHING
    PATTERN "*.js"
)

# Install support report script
install(PROGRAMS scripts/plasmazones-report.sh
    DESTINATION ${KDE_INSTALL_BINDIR}
    RENAME plasmazones-report
)

# Install icons (hicolor for dark themes, hicolor-light for light themes).
# The phosphor-shell.svg icons are conditional on BUILD_PHOSPHOR_SHELL —
# no point shipping the icon if the binary isn't built.
set(_plasmazones_dark_icons
    icons/hicolor/scalable/apps/plasmazones.svg
    icons/hicolor/scalable/apps/plasmazones-editor.svg
    icons/hicolor/scalable/apps/plasmazones-settings.svg
)
set(_plasmazones_light_icons
    icons/hicolor-light/scalable/apps/plasmazones.svg
    icons/hicolor-light/scalable/apps/plasmazones-editor.svg
    icons/hicolor-light/scalable/apps/plasmazones-settings.svg
)
if(BUILD_PHOSPHOR_SHELL)
    list(APPEND _plasmazones_dark_icons icons/hicolor/scalable/apps/phosphor-shell.svg)
    list(APPEND _plasmazones_light_icons icons/hicolor-light/scalable/apps/phosphor-shell.svg)
endif()
install(FILES ${_plasmazones_dark_icons}
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
install(FILES ${_plasmazones_light_icons}
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor-light/scalable/apps
)

if(BUILD_PHOSPHOR_SHELL)
    # Build the bundled example shell as a Qt QML module so qmlcachegen
    # and qmlsc run at build time. The resulting static module gets linked
    # into the phosphor-shell binary in src/CMakeLists.txt so the
    # executable has a working default that doesn't depend on filesystem
    # install (qrc fallback in ShellLoader::resolve()).
    add_subdirectory(examples/phosphor-shell)

    # Install the example phosphor-shell QML/shaders so users can copy
    # them into their config dir and edit. Same source as the qrc above —
    # qrc is the build-time frozen copy used as final fallback, this is
    # the user-editable copy. ShellLoader::resolve() probes
    # GenericConfigLocation (~/.config) → GenericDataLocation (this
    # install) → qrc:/ (linked-in fallback) in that order.
    install(DIRECTORY examples/phosphor-shell/
        DESTINATION ${KDE_INSTALL_DATADIR}/phosphor-shell
        FILES_MATCHING
            PATTERN "*.qml"
            PATTERN "*.frag"
            PATTERN "*.glsl"
    )
endif()

# Custom uninstall target that cleans up directories properly
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstalling PlasmaZones..."
)

# Build/install only installs files. Enabling the daemon, refreshing sycoca/KWin,
# and user-facing messages are handled by packaging (Arch .install, Debian postinst, RPM %post).

# Vulkan headers are needed at build time for QVulkanInstance (Vulkan backend support).
# OPTIONAL so PlasmaZones builds silently on systems without Vulkan dev packages -
# the Vulkan backend simply won't be available at runtime.
# All Vulkan-specific C++ code is guarded with #if QT_CONFIG(vulkan).
# Note: packaging specs (PKGBUILD, debian/control, rpm .spec, nix) list Vulkan as a
# hard BuildRequires to ensure the backend is always available in distribution packages.
set_package_properties(WrapVulkanHeaders PROPERTIES TYPE OPTIONAL)

# Feature summary (REQUIRED + OPTIONAL only; RUNTIME hidden to reduce noise)
feature_summary(WHAT REQUIRED_PACKAGES_FOUND REQUIRED_PACKAGES_NOT_FOUND
                     OPTIONAL_PACKAGES_FOUND OPTIONAL_PACKAGES_NOT_FOUND
                FATAL_ON_MISSING_REQUIRED_PACKAGES)

# ═══════════════════════════════════════════════════════════════════════════════
# CPack — source tarball generation (`cpack --config CPackSourceConfig.cmake`
# or `make package_source`). Distro builds in release.yml still hand-roll
# tarballs for filename-control reasons; this exists so a release engineer
# can also bootstrap a tarball locally without the workflow.
# ═══════════════════════════════════════════════════════════════════════════════
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "fuddlesworth")
set(CPACK_PACKAGE_CONTACT "fuddlesworth <fuddlesworth@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Window tiling and autotiling for KDE Plasma")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_GENERATOR "TXZ;TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.github/"
    "/\\\\.claude/"
    "/\\\\.zed/"
    "/\\\\.ccache/"
    "/build/"
    "/build-.*/"
    "/install/"
    "/dist/"
    "/node_modules/"
    "/\\\\.cache/"
    "/\\\\.idea/"
    "/\\\\.vscode/"
    "/\\\\.DS_Store"
    "/CMakeUserPresets\\\\.json$"
    "/CMakeLists\\\\.txt\\\\.user"
    "/compile_commands\\\\.json$"
    "\\\\.qm$"
    "\\\\.swp$"
    "~$"
)
include(CPack)

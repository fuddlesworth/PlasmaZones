# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones - Window tiling and autotiling for KDE Plasma

cmake_minimum_required(VERSION 3.16)

project(PlasmaZones VERSION 3.1.2 LANGUAGES C CXX)

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
# Silence Qt 6.6+ deprecation warnings on qt_add_qml_module: opt into
# the new default for resource-prefix derivation (QTP0001) and the new
# default for QML module dependency resolution (QTP0004). These are
# the NEW behaviours we already rely on across phosphor-* and src/
# qt_add_qml_module calls.
# NOTE: CMake's if(POLICY) only understands CMP* CMake policies — for QTP*
# Qt policies it is ALWAYS false. Qt's documented existence test is the
# QT_KNOWN_POLICY_<policy> variable (set by the Qt CMake machinery from 6.5).
if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()
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
# supporting libraries (libs/phosphor-shell + the gated phosphor-service-*
# group; see the add_subdirectory block farther down in this file for
# the authoritative list) + the bundled `phosphor-shell` example binary
# + the example QML/shader tree under examples/phosphor-shell. Default
# OFF so packaging / downstream installs of PlasmaZones don't pick up
# shell artefacts before the shell ships. Enable explicitly for shell
# development:
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
# dependency). PlasmaZones is Wayland-only, Plasma 6 dropped X11 as a
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
# `**` is not a CMake glob primitive; it expands to the same regex as
# `*` (anything except `/`). GLOB_RECURSE with a plain `*.cpp` pattern
# already recurses into subdirectories, so the recursive form is what
# we want; using `**/*.cpp` would require an intermediate path segment
# and silently drop files directly under each root (e.g. src/phosphor_qml_i18n.cpp,
# any top-level kwin-effect/.cpp).
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ${CMAKE_SOURCE_DIR}/cli/*.cpp
    ${CMAKE_SOURCE_DIR}/cli/*.h
    ${CMAKE_SOURCE_DIR}/cli/*.hpp
    ${CMAKE_SOURCE_DIR}/kcm/*.cpp
    ${CMAKE_SOURCE_DIR}/kcm/*.h
    ${CMAKE_SOURCE_DIR}/kcm/*.hpp
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.cpp
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.h
    ${CMAKE_SOURCE_DIR}/kwin-effect/*.hpp
    ${CMAKE_SOURCE_DIR}/tests/*.cpp
    ${CMAKE_SOURCE_DIR}/tests/*.h
    ${CMAKE_SOURCE_DIR}/libs/*.cpp
    ${CMAKE_SOURCE_DIR}/libs/*.h
    ${CMAKE_SOURCE_DIR}/libs/*.hpp
    ${CMAKE_SOURCE_DIR}/examples/*.cpp
    ${CMAKE_SOURCE_DIR}/examples/*.h
    ${CMAKE_SOURCE_DIR}/examples/*.hpp
)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

# QML: qmlformat (from qt6-tools, optional)
# Same `**`-vs-recursion rationale as the clang-format glob above.
find_program(QMLFORMAT qmlformat)
file(GLOB_RECURSE ALL_QML_FILES
    ${CMAKE_SOURCE_DIR}/src/*.qml
    ${CMAKE_SOURCE_DIR}/cli/*.qml
    ${CMAKE_SOURCE_DIR}/kcm/*.qml
    ${CMAKE_SOURCE_DIR}/libs/*.qml
    ${CMAKE_SOURCE_DIR}/examples/*.qml
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
# Translations (Qt Linguist). Extracted to cmake/PhosphorTranslations.cmake
# (included in this scope) to keep this file's growth in check — it has
# since crept past the 800-line cap again; the next self-contained block
# to outgrow its section should be extracted the same way. See that file
# for the update-ts / lrelease detail.
# ---------------------------------------------------------------------------
include(cmake/PhosphorTranslations.cmake)


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
add_subdirectory(libs/phosphor-protocol)    # D-Bus wire types and service constants, needed by phosphor-screens (Resolver endpoint defaults)
add_subdirectory(libs/phosphor-fsloader)    # filesystem-backed loader scaffolding (WatchedDirectorySet + DirectoryLoader) - Core-only, needed by phosphor-rules's store watcher
# phosphor-registry: generic Registry<T> + 5 factory interfaces + plugin/metadata
# loaders. Unconditional (third-party hosts + demos consume it without the shell).
# Moved ahead of phosphor-shaders: ShaderRegistry now composes Registry<T> +
# MetadataPackLoader<T>, so the in-tree target must exist before shaders configures
# (else find_package resolves a stale installed copy). Depends on phosphor-fsloader.
add_subdirectory(libs/phosphor-registry)    # generic Registry<T> + 5 factory interfaces + plugin loader
add_subdirectory(libs/phosphor-rules)  # unified window/context rule engine (MatchExpression, RuleEvaluator) - depends on phosphor-identity + phosphor-protocol + phosphor-fsloader
add_subdirectory(libs/phosphor-layout-api)  # shared layout-preview contract (LayoutPreview, ILayoutSource)
add_subdirectory(libs/phosphor-config)      # schema-driven configuration + migration runner - needed by phosphor-zones
add_subdirectory(libs/phosphor-shaders)     # shader-domain primitives (BaseUniforms, IUniformExtension, ShaderRegistry)
add_subdirectory(libs/phosphor-surface)     # surface-shader pack subsystem (SurfaceShaderContract/Effect/Registry) - depends on phosphor-shaders/registry/fsloader
add_subdirectory(libs/phosphor-wayland)       # Wayland layer-shell QPA plugin + LayerSurface - needed by phosphor-screens
# ext-idle-notify-v1. Built unconditionally (it used to sit behind
# BUILD_PHOSPHOR_SHELL) because the DAEMON now depends on it: the KWin effect
# pauses decoration animation while the session is idle, and idleness is a Wayland
# CLIENT concern the compositor serves rather than consumes, so the daemon has to
# watch for it and push the result to the effect. Only needs phosphor-wayland, which
# is already unconditional.
add_subdirectory(libs/phosphor-service-idle)  # Wayland idle monitoring + inhibition
add_subdirectory(libs/phosphor-geometry)    # pure geometry math (coordinate transforms, overlap, min-size) - needed by phosphor-screens (swapper directional selector)
add_subdirectory(libs/phosphor-screens)     # screen-topology + virtual-screen subdivision - needed by phosphor-zones
add_subdirectory(libs/phosphor-zones)       # zone primitives + LayoutManager (depends on phosphor-config IBackend, phosphor-screens ScreenIdentity)
add_subdirectory(libs/phosphor-context-resolver) # frozen-snapshot per-screen mode + disable/lock cascade façade (depends on phosphor-zones for AssignmentEntry::Mode)
add_subdirectory(libs/phosphor-scripting)   # generic embedded Luau host (engine, sandbox, watchdog, marshalling) — vendored extern/luau
add_subdirectory(libs/phosphor-tiles)       # auto-tiling algorithm primitives (TilingAlgorithm, AlgorithmRegistry, SplitTree)
add_subdirectory(libs/phosphor-rendering)   # GPU shader rendering via Qt RHI
add_subdirectory(libs/phosphor-layer)       # wlr-layer-shell surface lifecycle management
add_subdirectory(libs/phosphor-shell-patterns) # UI-pattern recipes (axis 2) built on PhosphorLayer::Role
add_subdirectory(libs/phosphor-shortcuts)   # global shortcut backends + domain-free Registry
# PlasmaZones consumes the org.phosphor.animation QML module via the
# daemon, the editor, and the phosphor-shell binary. The QML plugin's
# QtQuickClockManager directly constructs QtQuickClock, which is gated
# behind PHOSPHOR_ANIMATION_QUICK in phosphor-animation's CMakeLists.
# Without it, the QML plugin emits an unresolved-symbol link error in
# every PlasmaZones binary.
#
# FORCE so a stale cached OFF (from a prior standalone configure of
# libs/phosphor-animation in this build directory, or an explicit
# -DPHOSPHOR_ANIMATION_QUICK=OFF on the parent build) cannot silently
# turn into a broken build. Surface the override loudly so a user who
# DID pass -DPHOSPHOR_ANIMATION_QUICK=OFF understands their request
# was rejected — and why.
if(DEFINED CACHE{PHOSPHOR_ANIMATION_QUICK} AND NOT PHOSPHOR_ANIMATION_QUICK)
    message(WARNING
        "PHOSPHOR_ANIMATION_QUICK=OFF was requested but every PlasmaZones binary "
        "links the org.phosphor.animation QML plugin which requires QtQuickClock. "
        "Forcing PHOSPHOR_ANIMATION_QUICK=ON for this configure. "
        "To build without QtQuickClock, configure libs/phosphor-animation standalone.")
endif()
set(PHOSPHOR_ANIMATION_QUICK ON CACHE BOOL "Build QtQuickClock (required by every PlasmaZones binary linking PhosphorAnimationQmlplugin)" FORCE)
add_subdirectory(libs/phosphor-animation)   # unified animation library: motion runtime, QML module, shaders, layer
add_subdirectory(libs/phosphor-audio)       # audio spectrum provider (CAVA backend)
add_subdirectory(libs/phosphor-surfaces)    # managed surface lifecycle (engine, keep-alive, scope gen)
add_subdirectory(libs/phosphor-overlay)     # per-screen shell-host + slot mechanism (rides on layer/shell-patterns/surfaces/animation/screens)
# ───────────────────────────────────────────────────────────────────────
# Phosphor SDK groundwork — gated behind BUILD_PHOSPHOR_SHELL (default off)
# ───────────────────────────────────────────────────────────────────────
# The Phase-1 foundation libraries (theme / popout / ipc), the phosphorctl
# CLI, and the phosphor-shell binary are consumed only by the bundled shell,
# the example demos, and third-party shells. NOTHING in the shipping
# PlasmaZones tiler (daemon / editor / settings / kwin-effect) links them,
# so building them in a default package is pure overhead and installs unused
# runtime artifacts (the Phosphor.Theme QML plugin, /usr/bin/phosphorctl)
# that a strict RPM build rejects as unpackaged files. Gate the whole tier;
# the matching example demos under examples/ are gated the same way. Order
# matters: phosphor-shell links theme/popout/ipc, so they precede it.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(libs/phosphor-theme)   # M3 color-token store + Phosphor.Theme QML singletons
    add_subdirectory(libs/phosphor-popout)  # central popout coordinator (lifetime, focus, scope arbitration)
    add_subdirectory(libs/phosphor-ipc)     # JSON-over-Unix-socket IpcRouter + IpcTarget QML type
    add_subdirectory(cli/phosphorctl)       # phosphorctl call / list / schema / subscribe
    add_subdirectory(libs/phosphor-shell)   # shell infrastructure and QML types
    add_subdirectory(libs/phosphor-shell-widgets)  # Phosphor.Widgets M3 atom library (Phase 3.1); depends on phosphor-theme
    add_subdirectory(libs/phosphor-shell-osd)      # Phosphor.OSD on-screen-display framework (Phase 3.3); depends on theme + widgets
    add_subdirectory(libs/phosphor-shell-notifications) # Phosphor.Notifications toast framework (Phase 3.4); depends on theme + widgets
endif()
add_subdirectory(libs/phosphor-engine)  # unified placement engine contracts + window registry (SHARED lib)
add_subdirectory(libs/phosphor-workspaces)  # virtual desktop / workspace management
add_subdirectory(libs/phosphor-tile-engine) # autotile placement engine (IAutotileSettings + engine code)
add_subdirectory(libs/phosphor-snap-engine) # zone-based snap placement engine


add_subdirectory(libs/phosphor-placement)   # window placement state tracking

# Canonical list of engine library target names. Consumed by the
# engine QML firewall below; declared here next to the engine
# subdirs so the list moves with the targets it gates.
set(PHOSPHOR_ENGINE_TARGETS
    PhosphorZones
    PhosphorSnapEngine
    PhosphorTileEngine
    PhosphorEngine
    PhosphorWorkspaces
    PhosphorPlacement
)

# ─── Engine QML firewall ─────────────────────────────────────────────────────
#
# Engine libraries hold authoritative placement/snap/zone state and must
# stay free of the QML/JS engine. The moment one of them links Qt6::Qml or
# Qt6::Quick, the cost model changes: every QObject in the engine gets a
# JS scope, bindings cross language boundaries, and ownership becomes
# implicit through QQmlEngine. That's the trajectory Noctalia couldn't
# escape, their engines and config were both QML, so multi-day uptime
# leaks (FD exhaustion, GC pauses, JSValue retention across reloads) had
# nowhere to be fixed without a full rewrite.
#
# Refuse to configure if any engine target ever picks up a Qml/Quick
# link. Cheap guardrail over the targets' DIRECT link lists only — an
# intermediate in-tree lib that PUBLICly links Qt6::Qml would pass
# undetected (the engine target's own entries name the lib, not Qml).
# Failures here mean someone linked Qml/Quick onto an engine target
# directly or wired a target up wrong.
#
# Track whether ANY entry matched a real target so the firewall can't
# silently degrade to a no-op if PHOSPHOR_ENGINE_TARGETS goes stale
# (e.g. an engine lib gets renamed but the list doesn't follow). A
# zero-match firewall would let a future Qml link through unnoticed,
# which is exactly the failure mode the rationale above is designed
# to prevent.
set(_p_engine_matched_any FALSE)
foreach(_p_engine_lib IN LISTS PHOSPHOR_ENGINE_TARGETS)
    if(TARGET ${_p_engine_lib})
        set(_p_engine_matched_any TRUE)
        get_target_property(_p_engine_links ${_p_engine_lib} LINK_LIBRARIES)
        if(_p_engine_links)
            foreach(_p_link IN LISTS _p_engine_links)
                if(_p_link MATCHES "(^|::)Qt6::(Qml|Quick)(Private)?$")
                    message(FATAL_ERROR
                        "Engine library '${_p_engine_lib}' links '${_p_link}'. "
                        "Engine libs must stay QML-free, see comment above this "
                        "check in CMakeLists.txt for rationale.")
                endif()
            endforeach()
        endif()
        get_target_property(_p_engine_iface ${_p_engine_lib} INTERFACE_LINK_LIBRARIES)
        if(_p_engine_iface)
            foreach(_p_link IN LISTS _p_engine_iface)
                if(_p_link MATCHES "(^|::)Qt6::(Qml|Quick)(Private)?$")
                    message(FATAL_ERROR
                        "Engine library '${_p_engine_lib}' publicly links '${_p_link}'. "
                        "Engine libs must stay QML-free, see comment above this "
                        "check in CMakeLists.txt for rationale.")
                endif()
            endforeach()
        endif()
    endif()
endforeach()
if(NOT _p_engine_matched_any)
    message(FATAL_ERROR
        "Engine QML firewall matched zero targets from PHOSPHOR_ENGINE_TARGETS "
        "(${PHOSPHOR_ENGINE_TARGETS}). The firewall is a no-op in this state, "
        "which defeats its purpose. Either an engine lib was renamed without "
        "updating the list, the add_subdirectory order changed so engines "
        "haven't been declared yet at this point, or every engine subdir got "
        "gated behind an option that's currently OFF. Update "
        "PHOSPHOR_ENGINE_TARGETS to match the real engine target names, or "
        "move this firewall block below the add_subdirectory calls that "
        "declare the engines.")
endif()
add_subdirectory(libs/phosphor-compositor)  # compositor-plugin SDK
add_subdirectory(libs/phosphor-control) # reusable Qt6/QML/Kirigami settings-app framework (page controllers, registry, chrome)
if(BUILD_PHOSPHOR_SHELL)
    # Order: icontheme is consumed by sni (SNI publisher uses the
    # image provider + theme resolver), so configure it first. The
    # other Phase 2.0 siblings are independent. Phase 2.0 is complete:
    # the phosphor-services umbrella has been dissolved into these
    # per-domain libraries.
    add_subdirectory(libs/phosphor-service-icontheme) # XDG icon-theme resolver + Qt image provider
    add_subdirectory(libs/phosphor-service-sni)       # StatusNotifierItem host + watcher + dbusmenu
    add_subdirectory(libs/phosphor-service-upower)    # UPower battery / power-supply readouts
    add_subdirectory(libs/phosphor-service-mpris)     # MPRIS2 media-player discovery + control
    add_subdirectory(libs/phosphor-service-pipewire)  # PipeWire mixer (Phase 2.1, shipped)
    add_subdirectory(libs/phosphor-service-network)   # NetworkManager devices / connectivity (Phase 2.2)
    add_subdirectory(libs/phosphor-service-bluetooth) # BlueZ adapters / devices / pairing (Phase 2.3)
    add_subdirectory(libs/phosphor-service-brightness) # display / keyboard backlight brightness (Phase 2.4)
    add_subdirectory(libs/phosphor-service-notifications) # org.freedesktop.Notifications server (Phase 2.5)
    add_subdirectory(libs/phosphor-service-polkit)        # PolicyKit authentication agent (Phase 2.6)
    add_subdirectory(libs/phosphor-service-clipboard)     # Wayland clipboard history (data-control) (Phase 2.8)
    add_subdirectory(libs/phosphor-service-lock)          # PAM auth + ext-session-lock-v1 coordination (Phase 2.9)
    add_subdirectory(libs/phosphor-service-session)       # logind session/power actions + inhibitors (Phase 2.10)
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
# dbus/*.xml on disk, third-party clients (and qdbus-qt6 introspection)
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
    dbus/org.plasmazones.Snap.xml
    dbus/org.plasmazones.WindowDrag.xml
    dbus/org.plasmazones.Rules.xml
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
# don't manually concatenate CMAKE_INSTALL_PREFIX with KDE_INSTALL_DATADIR:
# that join produces `/usr//absolute/...` when ECM hands us an absolute
# KDE_INSTALL_DATADIR (some distros override it that way).
install(CODE "
    set(_p_profiles_install_dir \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATADIR}/plasmazones/profiles\")
    if(EXISTS \"\${_p_profiles_install_dir}\")
        file(GLOB _p_stale_profile_jsons \"\${_p_profiles_install_dir}/*.json\")
        if(_p_stale_profile_jsons)
            message(STATUS \"Removing stale animation profile JSONs from \${_p_profiles_install_dir}\")
            file(REMOVE \${_p_stale_profile_jsons})
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
install(DIRECTORY data/overlays/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/overlays
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

# Install bundled surface shader packs (window border / rounded corners today;
# the third shader-pack category alongside shaders + animations). The kwin-effect
# discovers ${XDG_DATA_DIRS}/plasmazones/surface via the SurfaceShaderRegistry;
# user overrides at ~/.local/share/plasmazones/surface win.
install(DIRECTORY data/surface/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/surface
    FILES_MATCHING
    PATTERN "metadata.json"
    PATTERN "*.frag"
    PATTERN "*.vert"
    # GLSL include files (shared/surface_uniforms.glsl).
    PATTERN "*.glsl"
    PATTERN "*.png"
)

# Install bundled scripted tiling algorithms
install(DIRECTORY data/algorithms/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasmazones/algorithms
    FILES_MATCHING
    PATTERN "*.luau"
)

# Install support report script
install(PROGRAMS scripts/plasmazones-report.sh
    DESTINATION ${KDE_INSTALL_BINDIR}
    RENAME plasmazones-report
)

# Install icons (hicolor for dark themes, hicolor-light for light themes).
# The phosphor-shell.svg icons are conditional on BUILD_PHOSPHOR_SHELL:
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

# Example demos and CLI acceptance harnesses are not part of the installed
# product. The Nix flake builds from a source tree with examples/ stripped
# (flake.nix removes ./examples from the fileset), so only descend into the
# example subdirectories when the directory is actually present. A normal git
# checkout always has it, so this guard is a no-op there; it nests over the
# inner BUILD_PHOSPHOR_SHELL gates below.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")

# Phosphor SDK groundwork demos. These link the gated Phase-1 libraries
# (phosphor-theme / popout / ipc), so they are gated to match: with
# BUILD_PHOSPHOR_SHELL OFF those targets do not exist and an unconditional
# add_subdirectory would fail the CMake generate step.
#   phosphor-theme-demo  — swatch-sheet viewer, hot-reloads the PaletteStore.
#   phosphor-theme-cli   — headless wallpaper → matugen → palette → template.
#   phosphor-popout-demo — popout arbitration harness (popout + theme).
#   phosphor-registry-demo — in-process IBarWidgetFactory registration
#                            (registry + theme).
#   phosphor-ipc-demo    — three IpcTargets driven via phosphorctl.
#   phosphor-widgets-kitchen-sink — every Phosphor.Widgets atom in its
#                            enabled/disabled states (widgets + theme).
#   phosphor-bar-canvas-demo — connected-corner bar; a popout grows out of
#                            the bar as one Shape, toggled via PopoutService.
#   phosphor-osd-demo — OSDHost overlay; the four built-in OSDs via
#                            Registry<IOSDFactory>, driven by phosphorctl.
#   phosphor-toast-demo — ToastHost overlay fed by a real NotificationServer
#                            (notify-send) + buttons; DND exercises the rules seam.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-theme-demo)
    add_subdirectory(examples/phosphor-theme-cli)
    add_subdirectory(examples/phosphor-popout-demo)
    add_subdirectory(examples/phosphor-registry-demo)
    add_subdirectory(examples/phosphor-ipc-demo)
    add_subdirectory(examples/phosphor-widgets-kitchen-sink)
    add_subdirectory(examples/phosphor-bar-canvas-demo)
    add_subdirectory(examples/phosphor-osd-demo)
    add_subdirectory(examples/phosphor-toast-demo)
endif()

# phosphor-perscreen-demo. Acceptance harness for the Phosphor.Shell
# PerScreen helper (Phase 1.5). Opens a small window per monitor;
# hot-plug a display → window appears. Unlike the foundation-lib demos
# above, this one links the gated PhosphorShell::PhosphorShellQml target
# (PerScreen lives in libs/phosphor-shell), so it must be gated too: when
# BUILD_PHOSPHOR_SHELL is OFF that target does not exist and an
# unconditional add_subdirectory fails the CMake generate step.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-perscreen-demo)
endif()

# phosphor-service-pipewire-cli. Acceptance harness for the
# phosphor-service-pipewire library (Phase 2.1). Headless driver
# exercising the read paths (list sinks/sources/streams, default),
# write paths (set-volume, mute/unmute), and metadata writes
# (set-default-sink/source) against a live PipeWire + WirePlumber
# daemon. Gated to match libs/phosphor-service-pipewire itself: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the
# CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-pipewire-cli)
endif()

# phosphor-service-network-cli. Acceptance harness for the
# phosphor-service-network library (Phase 2.2). Headless driver
# exercising the read paths (status, list-devices, list-connections,
# list-aps) and write paths (scan, connect) against a live
# org.freedesktop.NetworkManager. Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI
# can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-network-cli)
endif()

# phosphor-service-bluetooth-cli. Acceptance harness for the
# phosphor-service-bluetooth library (Phase 2.3). Headless driver exercising
# the read paths (status, list-adapters, list-devices) and write paths
# (power, scan, pair, connect, trust, remove) against a live org.bluez, with
# an interactive pairing agent. Gated like the lib: when BUILD_PHOSPHOR_SHELL
# is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-bluetooth-cli)
endif()

# phosphor-service-brightness-cli. Acceptance harness for the
# phosphor-service-brightness library (Phase 2.4). Headless driver exercising
# the read path (list, get) and write path (set) against real sysfs backlights
# and logind. Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib
# target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-brightness-cli)
endif()

# phosphor-service-notifications-cli. Acceptance harness for the
# phosphor-service-notifications library (Phase 2.5). Both a server (watch: own
# org.freedesktop.Notifications and log incoming Notify + close events) and a
# client (send / close / info via D-Bus). Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-notifications-cli)
endif()

# phosphor-service-polkit-cli. Acceptance harness for the phosphor-service-polkit
# library (Phase 2.6). Runs as the session's PolicyKit authentication agent and
# answers PAM prompts (trigger with `pkexec true`). Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-polkit-cli)
endif()

# phosphor-service-idle-cli. Acceptance harness for the phosphor-service-idle
# library (Phase 2.7). Reports idle support, logs each idle stage as it fires
# with a wall-clock timestamp, and can hold an inhibition for a fixed duration
# (--inhibit-for). Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib
# target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-idle-cli)
endif()

# phosphor-service-clipboard-cli. Acceptance harness for the
# phosphor-service-clipboard library (Phase 2.8). Watches the clipboard, lists
# the history, and copies an entry back. Gated like the lib: when
# BUILD_PHOSPHOR_SHELL is OFF the lib target does not exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-clipboard-cli)
endif()

# phosphor-service-lock-cli. Acceptance harness for the phosphor-service-lock
# library (Phase 2.9). Authenticates the user (PAM) and drives the session lock.
# Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib target does not
# exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-lock-cli)
endif()

# phosphor-service-session-cli. Acceptance harness for the
# phosphor-service-session library (Phase 2.10). Drives the logind session
# capabilities + actions (status / lock / logout / suspend / ... / power-off).
# Gated like the lib: when BUILD_PHOSPHOR_SHELL is OFF the lib target does not
# exist, so the CLI can't link.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-service-session-cli)
endif()

# phosphor-registry-plugin-demo. Sibling of phosphor-registry-demo
# that exercises the PluginLoader + hot-reload path. The third
# (CPU-meter) widget builds as a separate .so + manifest.json and
# is loaded via PluginLoader from a per-build plugin root. Links
# PhosphorTheme, so it is gated with the other SDK groundwork demos.
if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(examples/phosphor-registry-plugin-demo)
endif()

if(BUILD_PHOSPHOR_SHELL)
    # Build the bundled example shell as a Qt QML module so qmlcachegen
    # and qmlsc run at build time. The resulting static module gets linked
    # into the phosphor-shell binary in src/CMakeLists.txt so the
    # executable has a working default that doesn't depend on filesystem
    # install (qrc fallback in ShellLoader::resolve()).
    add_subdirectory(examples/phosphor-shell)

    # Install the example phosphor-shell QML/shaders so users can copy
    # them into their config dir and edit. Same source as the qrc above.
    # The qrc is the build-time frozen copy used as final fallback. This
    # install is the user-editable copy. ShellLoader::resolve() probes
    # GenericConfigLocation (~/.config), then GenericDataLocation (this
    # install), then qrc:/ (linked-in fallback) in that order.
    install(DIRECTORY examples/phosphor-shell/
        DESTINATION ${KDE_INSTALL_DATADIR}/phosphor-shell
        FILES_MATCHING
            PATTERN "*.qml"
            PATTERN "*.frag"
            PATTERN "*.glsl"
    )
endif()

endif() # examples/ present (Nix strips it; see the guard above)

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

# Qt's WrapVulkanHeaders backs the QVulkanInstance Vulkan-backend support, which
# is guarded throughout with #if QT_CONFIG(vulkan); it stays OPTIONAL here so that
# backend degrades gracefully when absent.
# NOTE: this does NOT make the whole build Vulkan-optional. The dma-buf thumbnail
# daemon path hard-requires the Vulkan SDK via find_package(Vulkan REQUIRED) in
# src/CMakeLists.txt (dmabuftextureprovider.cpp includes <vulkan/vulkan.h>
# unconditionally), so a daemon build without the Vulkan SDK fails at configure time.
# Packaging specs (PKGBUILD, debian/control, rpm .spec, nix) already list Vulkan as a
# hard BuildRequires, so distribution packages are unaffected.
set_package_properties(WrapVulkanHeaders PROPERTIES TYPE OPTIONAL)

# Feature summary (REQUIRED + OPTIONAL only; RUNTIME hidden to reduce noise)
feature_summary(WHAT REQUIRED_PACKAGES_FOUND REQUIRED_PACKAGES_NOT_FOUND
                     OPTIONAL_PACKAGES_FOUND OPTIONAL_PACKAGES_NOT_FOUND
                FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(cmake/PhosphorCPack.cmake)

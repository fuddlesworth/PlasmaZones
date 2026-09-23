# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones - Window snapping, tiling and scrolling for KDE Plasma

cmake_minimum_required(VERSION 3.16)

project(PlasmaZones VERSION 3.4.19 LANGUAGES C CXX)

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
# would wipe again), the suppression is applied per target rather than
# globally here. plasmazones/src/shared/CMakeLists.txt carries the full
# rationale. Dozens of CMakeLists across the tiers apply it, so do not read
# this as "one file opts out". For the current set, grep for the full
# `set_property(DIRECTORY APPEND PROPERTY COMPILE_OPTIONS "-Wno-missing-include-dirs")`
# line rather than the bare flag, which also matches this comment; and
# rather than a count written here, which drifts whenever a directory that
# builds a QML module is added.

# KDE/Qt requirements (Plasma 6.7 target: KF 6.26, Qt 6.10, KWin 6.7)
# Plasma 6.8 is supported by the same source: the KWin effect adapts to either
# version's paint API at compile time (plasmazones/kwin-effect/kwincompat.h), so these floors
# stay at the oldest supported Plasma rather than the newest.
set(QT_MIN_VERSION "6.10.0")
set(KF_MIN_VERSION "6.26.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
# Prepend our own cmake/ so project modules (PhosphorTestIsolation) resolve
# alongside ECM's.
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${ECM_MODULE_PATH})

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

# Make the Qt6 string-literal rule real. Without this, `const char*` converts
# implicitly to QString, so the CLAUDE.md rule ("QLatin1String for JSON keys
# and comparisons, QStringLiteral for constants, never a raw literal") was
# enforced only by reviewer attention and a raw literal compiled happily.
# With it, the conversion is gone and a violation is a compile error naming
# the exact line, which no pattern-matching linter can do as precisely.
#
# Verified clean before enabling: every TU in the compile database (1776, with
# BUILD_PHOSPHOR_SHELL=ON and BUILD_TOOLS=ON) was recompiled with the define
# and none reported a conversion error, so this fixed no existing code and can
# only catch new violations.
add_compile_definitions(QT_NO_CAST_FROM_ASCII)

# KDE Frameworks integration.
# ON (default): full KDE integration - KWin effect, KCM, KGlobalAccel shortcuts,
#               PlasmaActivities, KConfigXT for KCM defaults.
# OFF: portable Qt-only build - daemon + editor only, no KDE dependencies.
#      Shortcuts use XDG Portal or D-Bus trigger fallback.
option(USE_KDE_FRAMEWORKS "Build with KDE Frameworks integration" ON)

# Opt-in build for the in-progress phosphor-shell desktop shell and its
# supporting libraries (phosphor-shell-libs/libs/phosphor-shell plus the
# gated phosphor-service-* group; phosphor-shell-libs/CMakeLists.txt holds
# the authoritative list, since this file now only enters the two tier
# directories). Also builds the phosphor-shell binary and phosphorctl, and
# the bundled shell QML tree under phosphor-shell/shell/. Default OFF so
# packaging / downstream installs of PlasmaZones don't pick up shell
# artefacts before the shell ships. Enable explicitly for shell
# development:
#   cmake -DBUILD_PHOSPHOR_SHELL=ON
option(BUILD_PHOSPHOR_SHELL "Build the phosphor-shell desktop shell (work in progress)" OFF)

# The shell tier is exempt from the portable Qt-only invariant: the bar and
# power modules hard-require KF6 Kirigami (icon resolution through
# Kirigami.Icon), so a Qt-only shell build has nothing to draw icons with.
# Fail here with the reason rather than letting the nested
# find_package(KF6Kirigami REQUIRED) raise a bare not-found later.
if(BUILD_PHOSPHOR_SHELL AND NOT USE_KDE_FRAMEWORKS)
    message(FATAL_ERROR
        "BUILD_PHOSPHOR_SHELL requires USE_KDE_FRAMEWORKS=ON: the shell tier "
        "(phosphor-shell-libs/libs/phosphor-shell-bar, phosphor-shell-libs/libs/phosphor-shell-power, phosphor-shell-libs/libs/phosphor-shell-control-center, phosphor-shell-libs/libs/phosphor-shell-launcher, phosphor-shell-libs/libs/phosphor-shell-lock) depends on KF6 "
        "Kirigami for icon rendering and has no Qt-only fallback.")
endif()

# App-tier compile-time gate for the shell. The Phosphor shell's own surfaces
# (the `shell.phosphor.*` decoration subtree: bar, popouts, OSD bands, toasts,
# picker, lock) only exist when the shell is built, so the seeds in
# ConfigDefaults, the settings cards and their breadcrumb labels compile out
# with it. Defined at directory scope here, before any add_subdirectory, so
# every TU that includes the inline ConfigDefaults headers sees the same
# value (a per-target definition would split the ODR across core / daemon /
# settings / tests).
if(BUILD_PHOSPHOR_SHELL)
    add_compile_definitions(PLASMAZONES_HAVE_PHOSPHOR_SHELL)
endif()

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
# and silently drop files directly under each root (e.g. plasmazones/src/phosphor_qml_i18n.cpp,
# any top-level kwin-effect/.cpp).
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/plasmazones/src/*.cpp
    ${CMAKE_SOURCE_DIR}/plasmazones/src/*.h
    ${CMAKE_SOURCE_DIR}/plasmazones/src/*.hpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/cli/*.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/cli/*.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/cli/*.hpp
    ${CMAKE_SOURCE_DIR}/plasmazones/kcm/*.cpp
    ${CMAKE_SOURCE_DIR}/plasmazones/kcm/*.h
    ${CMAKE_SOURCE_DIR}/plasmazones/kcm/*.hpp
    ${CMAKE_SOURCE_DIR}/plasmazones/kwin-effect/*.cpp
    ${CMAKE_SOURCE_DIR}/plasmazones/kwin-effect/*.h
    ${CMAKE_SOURCE_DIR}/plasmazones/kwin-effect/*.hpp
    ${CMAKE_SOURCE_DIR}/plasmazones/tests/*.cpp
    ${CMAKE_SOURCE_DIR}/plasmazones/tests/*.h
    ${CMAKE_SOURCE_DIR}/phosphor/libs/*.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/libs/*.cpp
    ${CMAKE_SOURCE_DIR}/phosphor/libs/*.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/libs/*.h
    ${CMAKE_SOURCE_DIR}/phosphor/libs/*.hpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/libs/*.hpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/examples/*.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/*.cpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/examples/*.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell/*.h
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/examples/*.hpp
    ${CMAKE_SOURCE_DIR}/phosphor-shell/*.hpp
    # Built only with -DBUILD_TOOLS=ON, but this glob is filesystem-based
    # rather than target-based, so the entry is unconditional. Without it
    # the clang-format target skipped shader-render and
    # settings-schema-dump entirely; only lefthook reached them.
    ${CMAKE_SOURCE_DIR}/plasmazones/tools/*.cpp
    ${CMAKE_SOURCE_DIR}/plasmazones/tools/*.h
    ${CMAKE_SOURCE_DIR}/plasmazones/tools/*.hpp
)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

# QML: qmlformat (from qt6-tools, optional)
# Same `**`-vs-recursion rationale as the clang-format glob above.
find_program(QMLFORMAT qmlformat)
file(GLOB_RECURSE ALL_QML_FILES
    ${CMAKE_SOURCE_DIR}/plasmazones/src/*.qml
    ${CMAKE_SOURCE_DIR}/phosphor-shell/cli/*.qml
    ${CMAKE_SOURCE_DIR}/plasmazones/kcm/*.qml
    ${CMAKE_SOURCE_DIR}/phosphor/libs/*.qml
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/libs/*.qml
    ${CMAKE_SOURCE_DIR}/phosphor-shell-libs/examples/*.qml
    ${CMAKE_SOURCE_DIR}/phosphor-shell/*.qml
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
# and included in this scope. Other self-contained blocks can be extracted the
# same way. See that file for the update-ts / lrelease detail.
# ---------------------------------------------------------------------------
# Must stay ABOVE add_subdirectory(plasmazones): that tier's CMakeLists reads
# plasmazones_translations, Qt6LinguistTools_FOUND and TRANSLATION_TS_FILES from
# this scope to attach the .qm catalogs to plasmazones-tier. Moved below it, the
# target and both variables are simply absent there, so the if() is false AND the
# elseif() guard is false, and the attach fails open silently -- which is the
# exact failure that guard was added to stop.
include(cmake/PhosphorTranslations.cmake)

# Generate version header with compile-time version info
configure_file(
    "${CMAKE_SOURCE_DIR}/plasmazones/src/core/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)
include_directories("${CMAKE_BINARY_DIR}/generated")

# ═══════════════════════════════════════════════════════════════════════════════
# Tiers
# ═══════════════════════════════════════════════════════════════════════════════
# The tree is split into four product tiers, each a directory with its own
# CMakeLists.txt and, in the moon workspace, its own project:
#
#   phosphor/            core LGPL libraries (engines, rendering, layer-shell,
#                        animation, config, ...) that need no shell
#   phosphor-shell-libs/ the Phosphor desktop shell libraries and services,
#                        gated behind BUILD_PHOSPHOR_SHELL
#   phosphor-shell/      the phosphor-shell binary, phosphorctl and the shell
#                        QML tree, gated behind BUILD_PHOSPHOR_SHELL
#   plasmazones/         the PlasmaZones daemon, editor, settings, KCM, KWin
#                        effect, bundled data and tests
#
# Every tier CMakeLists is entered from here, so the shared Qt/KF6 setup
# above applies to all of them. Order follows the dependency chain, with
# one known inversion: phosphor-shell links plasmazones_rendering and
# plasmazones_shared_qml from the plasmazones tier (ShellChrome draws the
# surface decoration through them), so it is entered last. Target-based
# linking resolves at generate time, so the order is documentation, not
# a constraint.
add_subdirectory(phosphor)

# Canonical list of engine library target names. Consumed by the
# engine QML firewall below. Kept in sync with the engine
# add_subdirectory calls by hand; the firewall FATAL_ERRORs naming any
# entry that matches no target, so a single renamed engine cannot quietly
# stop being guarded.
set(PHOSPHOR_ENGINE_TARGETS
    PhosphorZones
    PhosphorSnapEngine
    PhosphorTileEngine
    PhosphorScrollEngine
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
set(_p_engine_unmatched "")
foreach(_p_engine_lib IN LISTS PHOSPHOR_ENGINE_TARGETS)
    if(NOT TARGET ${_p_engine_lib})
        list(APPEND _p_engine_unmatched ${_p_engine_lib})
    endif()
    if(TARGET ${_p_engine_lib})
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
if(_p_engine_unmatched)
    # PER-ENTRY, not "did anything match". Every name in the list is an
    # unconditional target by this point in the file, so a name that matches
    # nothing means that ONE engine silently stopped being guarded while the
    # others still are — the realistic staleness mode, and one a
    # matched-anything check sails straight past.
    message(FATAL_ERROR
        "Engine QML firewall matched no target for: ${_p_engine_unmatched}. "
        "Those entries of PHOSPHOR_ENGINE_TARGETS guard nothing, so a Qml "
        "link into them would go unnoticed. Either the library was renamed "
        "without updating the list, the add_subdirectory order changed so it "
        "has not been declared yet at this point, or it got gated behind an "
        "option that is currently OFF. Update PHOSPHOR_ENGINE_TARGETS to "
        "match the real engine target names, or move this firewall block "
        "below the add_subdirectory calls that declare the engines.")
endif()

if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(phosphor-shell-libs)
endif()

add_subdirectory(plasmazones)

if(BUILD_PHOSPHOR_SHELL)
    add_subdirectory(phosphor-shell)
endif()

# Custom uninstall target that cleans up directories properly.
# QT6_INSTALL_PLUGINS repeats phosphor-wayland's QPA install-path derivation
# (a subdirectory-scope variable there, so re-derived here for the configure
# below). The fallback there spells the libdir relative and here absolute,
# which resolves to the same place: install() roots a relative DESTINATION at
# the prefix, and the absolute branch below prepends the prefix itself.
if(NOT DEFINED QT6_INSTALL_PLUGINS)
    get_target_property(QT6_INSTALL_PLUGINS Qt6::Core QT_INSTALL_PLUGINS)
    if(NOT QT6_INSTALL_PLUGINS)
        set(QT6_INSTALL_PLUGINS "${KDE_INSTALL_FULL_LIBDIR}/qt6/plugins")
    endif()
endif()
# The Qt property can be prefix-relative; the uninstall script needs an
# absolute path (matching how install() resolves relative DESTINATIONs).
if(IS_ABSOLUTE "${QT6_INSTALL_PLUGINS}")
    set(PZ_UNINSTALL_QT_PLUGINS "${QT6_INSTALL_PLUGINS}")
else()
    set(PZ_UNINSTALL_QT_PLUGINS "${CMAKE_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
endif()
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
# plasmazones/src/CMakeLists.txt (dmabuftextureprovider.cpp includes <vulkan/vulkan.h>
# unconditionally), so a daemon build without the Vulkan SDK fails at configure time.
# Packaging specs (PKGBUILD, debian/control, rpm .spec, nix) already list Vulkan as a
# hard BuildRequires, so distribution packages are unaffected.
set_package_properties(WrapVulkanHeaders PROPERTIES TYPE OPTIONAL)

# Feature summary (REQUIRED + OPTIONAL only; RUNTIME hidden to reduce noise)
feature_summary(WHAT REQUIRED_PACKAGES_FOUND REQUIRED_PACKAGES_NOT_FOUND
                     OPTIONAL_PACKAGES_FOUND OPTIONAL_PACKAGES_NOT_FOUND
                FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(cmake/PhosphorCPack.cmake)

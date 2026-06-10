# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorScreens — domain-free screen-topology library for Qt6 Wayland.
#
# Three targets, layered so pure-compute consumers don't drag in the Wayland
# or QtDBus surface they never use:
#
#   PhosphorScreens::Core     — pure POD/serialiser surface: ScreenInfo,
#                               ScreenIdentity, Swapper, QtScreenProvider,
#                               VirtualScreen, and the IConfigStore /
#                               IPanelSource interfaces. Qt6::Core/Gui only —
#                               NO Wayland. POD-only consumers (zones, overlay)
#                               link this and avoid the PhosphorWayland .so.
#   PhosphorScreens::Runtime  — the live ScreenManager (physical-screen
#                               monitoring + virtual-screen subdivision) whose
#                               LayerSurface-backed geometry sensors need
#                               Wayland. Depends on ::Core + PhosphorWayland.
#                               Consumers that drive a ScreenManager (the
#                               daemon, snap/tile engines, placement) link this.
#   PhosphorScreens::PhosphorScreens
#                             — adds the D-Bus surface: DBusScreenAdaptor,
#                               the Plasma panel source, and the resolver.
#                               Depends on ::Runtime + Qt6::DBus.

cmake_minimum_required(VERSION 3.16)

# PhosphorScreens's own version — used for SOVERSION. When split to a
# standalone repo, this becomes the project() version.
set(PHOSPHORSCREENS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorScreens VERSION ${PHOSPHORSCREENS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# PhosphorScreens::Core is QtDBus-free (constants + screen-topology
# helpers only); the umbrella PhosphorScreens target adds the DBus
# wiring on top. Split the find_package() call so the Core dependency
# surface stays QtDBus-free: any consumer that only links ::Core (a
# domain library, a test fixture, etc.) doesn't pay for QtDBus and
# can't accidentally call into it via a transitively-resolved target.
#
# Both libraries are currently built unconditionally (there's no build
# option that gates the D-Bus layer out — the umbrella target is part
# of the lib's public contract), so the DBus find runs every time. If
# a future flag makes the umbrella target optional, the DBus find can
# be guarded behind it without revisiting consumer call sites.
#
# Namespace convention: this library exposes its types in a single-level
# namespace `PhosphorScreens` (not `Phosphor::Screens`) to align with
# other phosphor-* libs such as phosphor-control (`PhosphorControl`).
# A single-level namespace is the project convention for new phosphor-*
# libs; existing libs were flattened during the phosphor-control
# scaffolding pass. See libs/phosphor-screens/CHANGES.md for the rename
# history.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)
find_package(Qt6 6.6 REQUIRED COMPONENTS DBus)

# PhosphorIdentity ships VirtualScreenId — the cross-process ID format
# helpers that VirtualScreen.h consumes. INTERFACE-only dependency, so
# zero runtime cost and no .so to chase.
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# PhosphorWayland ships the wlr-layer-shell QPA integration. Only the Runtime
# target's ScreenManager consumes it (per-screen geometry sensor windows); the
# pure-POD Core target deliberately does NOT link it.
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland CONFIG REQUIRED)
endif()

# PhosphorProtocol::Types ships the QtCore-only org.plasmazones.* service /
# object / interface constants. Resolver.h pulls them in for the default
# endpoint — constants only, no QtDBus from this dependency.
if(NOT TARGET PhosphorProtocol::Types)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# Properties shared by all three library targets (Core, Runtime, umbrella).
function(_phosphorscreens_target_defaults _target)
    set_target_properties(${_target} PROPERTIES
        VERSION ${PHOSPHORSCREENS_VERSION}
        SOVERSION 0
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        UNITY_BUILD OFF
        AUTOMOC ON
        POSITION_INDEPENDENT_CODE ON
        # The shipped libraries link in a chain via their public interfaces:
        # PhosphorScreens.so → PhosphorScreensRuntime.so → PhosphorScreensCore.so.
        # On distros that don't add KDE_INSTALL_LIBDIR to the default loader
        # search path (e.g. /usr/local/lib), runtime load of a downstream .so
        # would fail with "...Core.so: cannot open shared object file". $ORIGIN
        # tells the dynamic linker to look beside the loading .so first, which is
        # exactly where all three shipped libs live. BUILD_WITH_INSTALL_RPATH lets
        # ctest dlopen the in-tree binaries without an explicit
        # LD_LIBRARY_PATH — without it, in-tree consumers (the demo,
        # the test runner) inherit CMake's empty build-tree rpath and
        # fail at sibling-lib resolution before install lands.
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
    )
    target_compile_features(${_target} PUBLIC cxx_std_20)
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorScreens::Core — pure-POD/serialiser surface (no Wayland, no QtDBus)
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscreenscore_public_HDRS
    include/PhosphorScreens/IConfigStore.h
    include/PhosphorScreens/InMemoryConfigStore.h
    include/PhosphorScreens/IPanelSource.h
    include/PhosphorScreens/IScreenProvider.h
    include/PhosphorScreens/NoOpPanelSource.h
    include/PhosphorScreens/PhysicalScreen.h
    include/PhosphorScreens/QtScreenProvider.h
    include/PhosphorScreens/ScreenIdentity.h
    include/PhosphorScreens/ScreenInfo.h
    include/PhosphorScreens/Swapper.h
    include/PhosphorScreens/VirtualScreen.h
)

add_library(PhosphorScreensCore SHARED
    ${phosphorscreenscore_public_HDRS}
    src/screenslogging.h
    src/qtscreenprovider.cpp
    src/screenidentity.cpp
    src/screeninfo.cpp
    src/screenslogging.cpp
    src/swapper.cpp
)

add_library(PhosphorScreens::Core ALIAS PhosphorScreensCore)

generate_export_header(PhosphorScreensCore
    BASE_NAME PhosphorScreensCore
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreenscore_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENSCORE_EXPORT
)

target_include_directories(PhosphorScreensCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorScreensCore
    PUBLIC
        Qt6::Core
        Qt6::Gui   # QRect / QRectF / QScreen in public headers
        PhosphorIdentity::PhosphorIdentity
)

_phosphorscreens_target_defaults(PhosphorScreensCore)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorScreens::Runtime — the live ScreenManager (needs Wayland)
# ═══════════════════════════════════════════════════════════════════════════════
#
# ScreenManager is one class split across two TUs (manager.cpp +
# manager_virtualscreens.cpp); only manager.cpp's geometry-sensor lifecycle
# touches PhosphorWayland (LayerSurface), but the class travels whole here so
# its symbols live in a single .so. Manager.h's public surface is Wayland-free
# (Qt types only), so Core consumers that never construct a ScreenManager don't
# need this target at all.

set(phosphorscreensruntime_public_HDRS
    include/PhosphorScreens/Manager.h
)

add_library(PhosphorScreensRuntime SHARED
    ${phosphorscreensruntime_public_HDRS}
    src/manager.cpp
    src/manager_virtualscreens.cpp
)

add_library(PhosphorScreens::Runtime ALIAS PhosphorScreensRuntime)

generate_export_header(PhosphorScreensRuntime
    BASE_NAME PhosphorScreensRuntime
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreensruntime_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENSRUNTIME_EXPORT
)

target_include_directories(PhosphorScreensRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorScreensRuntime
    PUBLIC
        # Manager.h surfaces QtScreenProvider / IConfigStore / VirtualScreen
        # (all Core POD types), so Core is a PUBLIC dependency.
        PhosphorScreens::Core
        Qt6::Core
        Qt6::Gui
    PRIVATE
        # LayerSurface geometry sensors — confined to manager.cpp, hence PRIVATE.
        PhosphorWayland::PhosphorWayland
)

_phosphorscreens_target_defaults(PhosphorScreensRuntime)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorScreens — D-Bus adaptor + Plasma panel source + resolver
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscreens_public_HDRS
    include/PhosphorScreens/DBusScreenAdaptor.h
    include/PhosphorScreens/PhosphorScreens.h
    include/PhosphorScreens/PlasmaPanelSource.h
    include/PhosphorScreens/Resolver.h
)

add_library(PhosphorScreens SHARED
    ${phosphorscreens_public_HDRS}
    src/dbusscreenadaptor.cpp
    src/plasmapanelsource.cpp
    src/resolver.cpp
)

add_library(PhosphorScreens::PhosphorScreens ALIAS PhosphorScreens)

generate_export_header(PhosphorScreens
    BASE_NAME PhosphorScreens
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENS_EXPORT
)

target_include_directories(PhosphorScreens
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorScreens
    PUBLIC
        # DBusScreenAdaptor holds a ScreenManager* and connects its signals,
        # so the umbrella needs Runtime (which re-exports Core).
        PhosphorScreens::Runtime
        Qt6::DBus  # QDBusAbstractAdaptor in DBusScreenAdaptor.h public surface
        PhosphorProtocol::Types  # Service::* constants in Resolver.h
)

_phosphorscreens_target_defaults(PhosphorScreens)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorScreensCore PhosphorScreensRuntime PhosphorScreens
    EXPORT PhosphorScreensTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorScreensTargets
    FILE PhosphorScreensTargets.cmake
    NAMESPACE PhosphorScreens::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorScreensConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfigVersion.cmake"
    VERSION ${PHOSPHORSCREENS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)

install(DIRECTORY include/PhosphorScreens/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScreens
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreenscore_export.h
    ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreensruntime_export.h
    ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScreens
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorScreens" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

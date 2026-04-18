# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorScreens — domain-free screen-topology library for Qt6 Wayland.
#
# Owns physical screen monitoring, virtual-screen subdivision, and
# effective-screen resolution. Pluggable backends for panel-offset
# detection (KDE Plasma D-Bus, no-op) and VS-config persistence
# (Settings, in-memory). See docs/phosphor-screens-api-design.md.
#
# Surface: VirtualScreen.h POD types, ScreenResolver, ScreenManager,
# IPanelSource (+ PlasmaPanelSource, NoOpPanelSource), IConfigStore
# (+ InMemoryConfigStore), VirtualScreenSwapper, DBusScreenAdaptor,
# ScreenIdentity helpers.

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

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui DBus)

# PhosphorIdentity ships VirtualScreenId — the cross-process ID format
# helpers that VirtualScreen.h consumes. INTERFACE-only dependency, so
# zero runtime cost and no .so to chase. Found via add_subdirectory in
# PZ in-tree builds; falls back to find_package when this library is
# consumed standalone. CONFIG + REQUIRED matches the pattern sibling
# phosphor-* libs use (phosphor-zones, phosphor-tiles).
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# PhosphorShell ships the wlr-layer-shell QPA integration. ScreenManager
# consumes it for the per-screen geometry sensor windows.
if(NOT TARGET PhosphorShell::PhosphorShell)
    find_package(PhosphorShell CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscreens_public_HDRS
    include/PhosphorScreens/DBusScreenAdaptor.h
    include/PhosphorScreens/IConfigStore.h
    include/PhosphorScreens/InMemoryConfigStore.h
    include/PhosphorScreens/IPanelSource.h
    include/PhosphorScreens/Manager.h
    include/PhosphorScreens/NoOpPanelSource.h
    include/PhosphorScreens/PhosphorScreens.h
    include/PhosphorScreens/PlasmaPanelSource.h
    include/PhosphorScreens/Resolver.h
    include/PhosphorScreens/ScreenIdentity.h
    include/PhosphorScreens/Swapper.h
    include/PhosphorScreens/VirtualScreen.h
)

set(phosphorscreens_private_HDRS
    src/screenslogging.h
)

set(phosphorscreens_SRCS
    src/dbusscreenadaptor.cpp
    src/manager.cpp
    src/manager_virtualscreens.cpp
    src/plasmapanelsource.cpp
    src/resolver.cpp
    src/screenidentity.cpp
    src/screenslogging.cpp
    src/swapper.cpp
)

add_library(PhosphorScreens SHARED
    ${phosphorscreens_public_HDRS}
    ${phosphorscreens_private_HDRS}
    ${phosphorscreens_SRCS}
)

add_library(PhosphorScreens::PhosphorScreens ALIAS PhosphorScreens)

generate_export_header(PhosphorScreens
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENS_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR here so INSTALL_INTERFACE can reference it.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorScreens
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(PhosphorScreens PUBLIC cxx_std_20)

target_link_libraries(PhosphorScreens
    PUBLIC
        Qt6::Core
        Qt6::Gui   # QRect / QRectF / QScreen in public headers
        Qt6::DBus  # QDBusAbstractAdaptor in DBusScreenAdaptor.h public surface
        PhosphorIdentity::PhosphorIdentity
    PRIVATE
        PhosphorShell::PhosphorShell  # LayerSurface for geometry sensors
)

set_target_properties(PhosphorScreens PROPERTIES
    VERSION ${PHOSPHORSCREENS_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
    AUTOMOC ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorScreens
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
    # SameMajorVersion matches every other phosphor-* library in this
    # tree (phosphor-identity, phosphor-layer, phosphor-animation,
    # phosphor-tiles, phosphor-zones, phosphor-shell). Keep the policy
    # uniform across the family so consumers see one compatibility rule
    # rather than per-library divergence.
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

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScreens
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

# Enable unit tests when building as part of the parent project or when the
# user explicitly toggles BUILD_TESTING. Mirrors phosphor-animation's policy so
# ctest picks them up automatically on a normal `cmake --build` cycle.
if(CMAKE_PROJECT_NAME STREQUAL "PhosphorScreens" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

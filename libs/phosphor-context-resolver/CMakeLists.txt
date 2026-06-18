# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorContextResolver — frozen-snapshot effective-value + gate cascade.
#
# Wraps the four interlocking sources every Phosphor consumer currently
# stitches by hand (mode router + virtual-desktop + activity + disable/lock
# settings) into a single `ContextHandle` snapshot + `IContextResolver`
# façade. The open-coded `(modeFor(screenId) → currentDesktop()
# → currentActivity() → isContextDisabled(mode, …) → isContextLocked(…))`
# chains across the daemon and the D-Bus adaptors collapse to one resolver
# call per site, and adding a new override axis (per-application,
# per-window-type, future per-mode "scrolling" leaf) gains one resolver
# method instead of a fan-out sweep. The KWin effect does NOT consume
# this library — effect-side state has no disable/lock cascade.
#
# This library does NOT own any QObject / QML / D-Bus surface — those belong
# to the higher-level GPL settings app and adaptors. It also does NOT
# implement rule evaluation — that stays in PhosphorWindowRules.
#
# Dependency surface: Qt6::Core only (NO Qt6::Gui) + PhosphorZones (PUBLIC
# — AssignmentEntry::Mode leaks through the API). The library is LGPL so
# GPL daemons / effects can link without inheriting GPL.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORCONTEXTRESOLVER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorContextResolver VERSION ${PHOSPHORCONTEXTRESOLVER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# Core only — no Gui. The resolver is consumed by the daemon (which already
# links Qt6::Gui) but should never force a transitively-Gui link on future
# headless consumers (CLI tooling, daemon-side preview rendering).
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

# PhosphorZones supplies AssignmentEntry::Mode which the resolver's public
# headers surface (every gate query takes a Mode). PUBLIC because the type
# leaks through the API. Found via the in-tree subdirectory in Phosphor builds;
# falls back to find_package when this library is consumed standalone.
if(NOT TARGET PhosphorZones::PhosphorZones)
    find_package(PhosphorZones CONFIG REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorcontextresolver_public_HDRS
    include/PhosphorContext/PhosphorContext.h
    include/PhosphorContext/ContextHandle.h
    include/PhosphorContext/DisabledReason.h
    include/PhosphorContext/IContextInputs.h
    include/PhosphorContext/IContextResolver.h
    include/PhosphorContext/ContextResolver.h
)

set(phosphorcontextresolver_SRCS
    src/contextresolver.cpp
)

add_library(PhosphorContextResolver SHARED
    ${phosphorcontextresolver_public_HDRS}
    ${phosphorcontextresolver_SRCS}
)

add_library(PhosphorContextResolver::PhosphorContextResolver ALIAS PhosphorContextResolver)

generate_export_header(PhosphorContextResolver
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorcontextresolver_export.h
    EXPORT_MACRO_NAME PHOSPHORCONTEXTRESOLVER_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorContextResolver
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorContextResolver PUBLIC cxx_std_20)

target_link_libraries(PhosphorContextResolver
    PUBLIC
        Qt6::Core
        # AssignmentEntry::Mode is a PUBLIC API parameter on every gate
        # query. Consumers including any IContextResolver method must see
        # the enum.
        PhosphorZones::PhosphorZones
)

set_target_properties(PhosphorContextResolver PROPERTIES
    VERSION ${PHOSPHORCONTEXTRESOLVER_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorContextResolver" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

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

install(TARGETS PhosphorContextResolver
    EXPORT PhosphorContextResolverTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorContextResolverTargets
    FILE PhosphorContextResolverTargets.cmake
    NAMESPACE PhosphorContextResolver::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorContextResolver
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorContextResolverConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorContextResolverConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorContextResolver
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorContextResolverConfigVersion.cmake"
    VERSION ${PHOSPHORCONTEXTRESOLVER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorContextResolverConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorContextResolverConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorContextResolver
)

install(DIRECTORY include/PhosphorContext/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorContext
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorcontextresolver_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorContext
)

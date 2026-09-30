# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorPlacement — Window placement state tracking service.
#
# Tracks window→zone assignments, floating state, auto-snap, resnap,
# and rotation. Compositor-agnostic: any consumer that provides an
# IGeometryResolver gets full placement tracking without the daemon.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPLACEMENT_VERSION "0.1.0")

# The Qt floor this library requires AND advertises in its generated
# PhosphorPlacementConfig.cmake. Taken from the top-level QT_MIN_VERSION when built in-tree so
# the two cannot drift, with its own value for a STANDALONE build, where that
# variable does not exist. The fallback is not optional: the Config template
# substitutes this, and an undefined variable expands to nothing and ships a config
# whose find_dependency(Qt6 ...) carries no version floor at all.
if(DEFINED QT_MIN_VERSION)
    set(PHOSPHORPLACEMENT_QT_MIN ${QT_MIN_VERSION})
else()
    set(PHOSPHORPLACEMENT_QT_MIN "6.10.0")
endif()

if(NOT PROJECT_VERSION)
    project(PhosphorPlacement VERSION ${PHOSPHORPLACEMENT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

# Sibling Phosphor libraries this one links. Each lookup is GUARDED rather than
# unconditional: in-tree the parent add_subdirectory()s the sibling before this
# directory, so its ALIAS target already exists and the find_package is skipped.
# A STANDALONE configure (the project() guard above says this directory supports
# one) has no such target, and without these it failed at target_link_libraries
# with "links to X::X but the target was not found".

# the shared placement policy every engine builds on
if(NOT TARGET PhosphorEngine::PhosphorEngine)
    find_package(PhosphorEngine CONFIG REQUIRED)
endif()

# the window-identity types the placement API keys on
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# the layout-provider interface
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# the snapping engine
if(NOT TARGET PhosphorSnapEngine::PhosphorSnapEngine)
    find_package(PhosphorSnapEngine CONFIG REQUIRED)
endif()

# the workspace model
if(NOT TARGET PhosphorWorkspaces::PhosphorWorkspaces)
    find_package(PhosphorWorkspaces CONFIG REQUIRED)
endif()

# the zone and layout model
if(NOT TARGET PhosphorZones::PhosphorZones)
    find_package(PhosphorZones CONFIG REQUIRED)
endif()

# the D-Bus contract types. Linked as PhosphorProtocol::Types, an alias whose
# target name differs from the package name, which is why the first sweep
# over Pkg::Pkg spellings did not catch it.
if(NOT TARGET PhosphorProtocol::PhosphorProtocol)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

# the screen model. Linked as PhosphorScreens::Runtime, an alias whose target
# name differs from the package name.
if(NOT TARGET PhosphorScreens::PhosphorScreens)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

add_library(PhosphorPlacement SHARED
    include/PhosphorPlacement/IGeometryResolver.h
    include/PhosphorPlacement/PlacementConfig.h
    include/PhosphorPlacement/SnapStateResolver.h
    include/PhosphorPlacement/WindowTrackingService.h
    src/WindowTrackingService.cpp
    src/WindowTrackingService_sticky.cpp
    src/navigation.cpp
    src/resnap.cpp
    src/lifecycle.cpp
    src/virtualscreenmigration.cpp
    src/placementvalidation_p.h
    src/snap.cpp
    src/placementlogging.h
    src/placementlogging.cpp
)
add_library(PhosphorPlacement::PhosphorPlacement ALIAS PhosphorPlacement)

generate_export_header(PhosphorPlacement
    EXPORT_FILE_NAME phosphorplacement_export.h
    EXPORT_MACRO_NAME PHOSPHORPLACEMENT_EXPORT
)

target_include_directories(PhosphorPlacement
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorPlacement PUBLIC cxx_std_20)

target_link_libraries(PhosphorPlacement
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorEngine::PhosphorEngine
        PhosphorSnapEngine::PhosphorSnapEngine
        PhosphorZones::PhosphorZones
        PhosphorProtocol::Types
    PRIVATE
        PhosphorScreens::Runtime  # WindowTrackingService drives a ScreenManager*
        PhosphorIdentity::PhosphorIdentity
        PhosphorWorkspaces::PhosphorWorkspaces
        PhosphorLayoutApi::PhosphorLayoutApi
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

set_target_properties(PhosphorPlacement PROPERTIES
    VERSION ${PHOSPHORPLACEMENT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorPlacement
    EXPORT PhosphorPlacementTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorPlacement
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorplacement_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPlacement
)

install(EXPORT PhosphorPlacementTargets
    FILE PhosphorPlacementTargets.cmake
    NAMESPACE PhosphorPlacement::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorPlacementConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfigVersion.cmake"
    VERSION ${PHOSPHORPLACEMENT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPlacementConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPlacement
)

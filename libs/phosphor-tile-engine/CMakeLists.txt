# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorTileEngine — Autotile placement engine.
#
# Provides AutotileEngine + supporting classes (config, navigation,
# overflow, per-screen resolver). Depends on phosphor-engine for
# the IPlacementEngine contract and phosphor-tiles for tiling algorithms.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORTILEENGINE_VERSION "0.2.0")

if(NOT PROJECT_VERSION)
    project(PhosphorTileEngine VERSION ${PHOSPHORTILEENGINE_VERSION} LANGUAGES CXX)
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

# the geometry primitives the placement types are expressed in
if(NOT TARGET PhosphorGeometry::PhosphorGeometry)
    find_package(PhosphorGeometry CONFIG REQUIRED)
endif()

# the window-identity types the placement API keys on
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# the layout-provider interface
if(NOT TARGET PhosphorLayoutApi::PhosphorLayoutApi)
    find_package(PhosphorLayoutApi CONFIG REQUIRED)
endif()

# the Luau tiling-algorithm host
if(NOT TARGET PhosphorTiles::PhosphorTiles)
    find_package(PhosphorTiles CONFIG REQUIRED)
endif()

# the zone and layout model
if(NOT TARGET PhosphorZones::PhosphorZones)
    find_package(PhosphorZones CONFIG REQUIRED)
endif()

# the screen model. Linked as PhosphorScreens::Runtime, an alias whose target
# name differs from the package name.
if(NOT TARGET PhosphorScreens::PhosphorScreens)
    find_package(PhosphorScreens CONFIG REQUIRED)
endif()

add_library(PhosphorTileEngine SHARED
    include/PhosphorTileEngine/AutotileEngine.h
    include/PhosphorTileEngine/AutotileEngineTypes.h
    include/PhosphorTileEngine/AutotileConfig.h
    include/PhosphorTileEngine/NavigationController.h
    include/PhosphorTileEngine/OverflowManager.h
    include/PhosphorTileEngine/PerScreenConfigResolver.h
    include/PhosphorTileEngine/IAutotileSettings.h
    src/AutotileEngine.cpp
    src/autotileengine/engine_internal.h
    src/autotileengine/core.cpp
    src/autotileengine/context.cpp
    src/autotileengine/membership.cpp
    src/autotileengine/algorithm_state.cpp
    src/autotileengine/config.cpp
    src/autotileengine/retile.cpp
    src/autotileengine/float_handoff.cpp
    src/autotileengine/window_lifecycle.cpp
    src/autotileengine/resize.cpp
    src/autotileengine/insert.cpp
    src/autotileengine/layout_apply.cpp
    src/autotileengine/drag_preview.cpp
    src/autotileengine/facade.cpp
    src/AutotileConfig.cpp
    src/NavigationController.cpp
    src/NavigationController_crosssurface.cpp
    src/OverflowManager.cpp
    src/PerScreenConfigResolver.cpp
    src/tileenginelogging.h
    src/tileenginelogging.cpp
)
add_library(PhosphorTileEngine::PhosphorTileEngine ALIAS PhosphorTileEngine)

generate_export_header(PhosphorTileEngine
    EXPORT_FILE_NAME phosphortileengine_export.h
    EXPORT_MACRO_NAME PHOSPHORTILEENGINE_EXPORT
)

target_include_directories(PhosphorTileEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorTileEngine PUBLIC cxx_std_20)

# Same Qt6 / GCC 14 -Wsfinae-incomplete interaction as the
# PhosphorZones target: this engine includes Layout.h after a
# forward decl elsewhere in the include graph, tripping the
# SFINAE-incomplete diagnostic on Layout's full definition. See
# libs/phosphor-zones/CMakeLists.txt for the full rationale.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-sfinae-incomplete" PTE_HAS_NO_SFINAE_INCOMPLETE)
if(PTE_HAS_NO_SFINAE_INCOMPLETE)
    target_compile_options(PhosphorTileEngine PRIVATE -Wno-sfinae-incomplete)
endif()

target_link_libraries(PhosphorTileEngine
    PUBLIC
        Qt6::Core
        PhosphorEngine::PhosphorEngine
        PhosphorTiles::PhosphorTiles
        # PUBLIC: PerScreenConfigResolver.h (a public header) includes
        # <PhosphorLayoutApi/EdgeGaps.h> and exposes ::PhosphorLayout::EdgeGaps
        # in its API, so downstream consumers need this on their include path.
        PhosphorLayoutApi::PhosphorLayoutApi
    PRIVATE
        PhosphorScreens::Runtime  # AutotileEngine queries screen geometry via ScreenManager
        PhosphorIdentity::PhosphorIdentity
        PhosphorZones::PhosphorZones
        PhosphorGeometry::PhosphorGeometry  # geometric directional-neighbour selection
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

set_target_properties(PhosphorTileEngine PROPERTIES
    VERSION ${PHOSPHORTILEENGINE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorTileEngine
    EXPORT PhosphorTileEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorTileEngine
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphortileengine_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorTileEngine
)

install(EXPORT PhosphorTileEngineTargets
    FILE PhosphorTileEngineTargets.cmake
    NAMESPACE PhosphorTileEngine::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTileEngine
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorTileEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTileEngineConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTileEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTileEngineConfigVersion.cmake"
    VERSION ${PHOSPHORTILEENGINE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTileEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorTileEngineConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorTileEngine
)

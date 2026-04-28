# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorTileEngine — Autotile placement engine.
#
# Provides AutotileEngine + supporting classes (config, navigation,
# overflow, per-screen resolver). Depends on phosphor-engine-api for
# the IPlacementEngine contract and phosphor-tiles for tiling algorithms.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORTILEENGINE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorTileEngine VERSION ${PHOSPHORTILEENGINE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorTileEngine SHARED
    include/PhosphorTileEngine/AutotileEngine.h
    include/PhosphorTileEngine/AutotileConfig.h
    include/PhosphorTileEngine/NavigationController.h
    include/PhosphorTileEngine/OverflowManager.h
    include/PhosphorTileEngine/PerScreenConfigResolver.h
    include/PhosphorTileEngine/IAutotileSettings.h
    src/AutotileEngine.cpp
    src/AutotileConfig.cpp
    src/NavigationController.cpp
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

target_link_libraries(PhosphorTileEngine
    PUBLIC
        Qt6::Core
        PhosphorEngineApi::PhosphorEngineApi
        PhosphorTiles::PhosphorTiles
    PRIVATE
        PhosphorScreens::PhosphorScreens
        PhosphorIdentity::PhosphorIdentity
        PhosphorZones::PhosphorZones
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS PhosphorTileEngine
    EXPORT PhosphorTileEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
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

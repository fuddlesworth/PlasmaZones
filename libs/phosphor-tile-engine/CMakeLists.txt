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

# Currently INTERFACE (header-only: IAutotileSettings). Will become SHARED
# when engine source files are moved from src/autotile/.
add_library(PhosphorTileEngine INTERFACE)
add_library(PhosphorTileEngine::PhosphorTileEngine ALIAS PhosphorTileEngine)

target_include_directories(PhosphorTileEngine
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorTileEngine INTERFACE cxx_std_20)

target_link_libraries(PhosphorTileEngine
    INTERFACE
        Qt6::Core
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

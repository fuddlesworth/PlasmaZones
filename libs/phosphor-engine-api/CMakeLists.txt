# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorEngineApi — Unified placement engine contracts + base class.
#
# Shared library providing IPlacementEngine, IPlacementState,
# NavigationContext, and PlacementEngineBase. Both phosphor-zones (snap)
# and phosphor-tiles (autotile) inherit PlacementEngineBase so daemons
# and compositor plugins get universal window-state mechanics for free.
#
# Depends on Qt6::Core (QString, QStringList, QRect, QJsonObject, QObject).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORENGINEAPI_VERSION "0.2.0")

if(NOT PROJECT_VERSION)
    project(PhosphorEngineApi VERSION ${PHOSPHORENGINEAPI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorEngineApi Library (SHARED)
# ═══════════════════════════════════════════════════════════════════════════════

set(CMAKE_AUTOMOC ON)

add_library(PhosphorEngineApi SHARED
    include/PhosphorEngineApi/PlacementEngineBase.h
    src/PlacementEngineBase.cpp
    src/GeometryUtils.cpp
)
add_library(PhosphorEngineApi::PhosphorEngineApi ALIAS PhosphorEngineApi)

generate_export_header(PhosphorEngineApi
    EXPORT_FILE_NAME phosphorengineapi_export.h
    EXPORT_MACRO_NAME PHOSPHORENGINEAPI_EXPORT
)

target_include_directories(PhosphorEngineApi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorEngineApi PUBLIC cxx_std_20)

target_link_libraries(PhosphorEngineApi
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorEngineTypes::PhosphorEngineTypes
        PhosphorGeometry::PhosphorGeometry
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS PhosphorEngineApi
    EXPORT PhosphorEngineApiTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorEngineApi
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorengineapi_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorEngineApi
)

install(EXPORT PhosphorEngineApiTargets
    FILE PhosphorEngineApiTargets.cmake
    NAMESPACE PhosphorEngineApi::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorEngineApiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfigVersion.cmake"
    VERSION ${PHOSPHORENGINEAPI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)

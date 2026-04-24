# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorGeometry — Pure geometry math utilities.
#
# Zero phosphor dependencies. Provides coordinate transforms, overlap
# resolution, min-size enforcement, and JSON serialization for zone
# geometry. Used by engines, overlay, and daemon code.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORGEOMETRY_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorGeometry VERSION ${PHOSPHORGEOMETRY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

add_library(PhosphorGeometry SHARED
    src/GeometryUtils.cpp
)
add_library(PhosphorGeometry::PhosphorGeometry ALIAS PhosphorGeometry)

generate_export_header(PhosphorGeometry
    EXPORT_FILE_NAME phosphorgeometry_export.h
    EXPORT_MACRO_NAME PHOSPHORGEOMETRY_EXPORT
)

target_include_directories(PhosphorGeometry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorGeometry PUBLIC cxx_std_20)

target_link_libraries(PhosphorGeometry
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS PhosphorGeometry
    EXPORT PhosphorGeometryTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorGeometry
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorgeometry_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorGeometry
)

install(EXPORT PhosphorGeometryTargets
    FILE PhosphorGeometryTargets.cmake
    NAMESPACE PhosphorGeometry::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorGeometry
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorGeometryConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorGeometryConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorGeometry
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorGeometryConfigVersion.cmake"
    VERSION ${PHOSPHORGEOMETRY_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorGeometryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorGeometryConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorGeometry
)

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorGeometry — Pure geometry math library.
#
# Zero phosphor dependencies, Qt6::Core only. Provides coordinate
# transforms, overlap resolution, min-size enforcement, and JSON
# serialization for zone geometry.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORGEOMETRY_VERSION "0.1.0")

# The Qt floor this library requires AND advertises in its generated
# PhosphorGeometryConfig.cmake. Taken from the top-level QT_MIN_VERSION when built in-tree so
# the two cannot drift, with its own value for a STANDALONE build, where that
# variable does not exist. The fallback is not optional: the Config template
# substitutes this, and an undefined variable expands to nothing and ships a config
# whose find_dependency(Qt6 ...) carries no version floor at all.
if(DEFINED QT_MIN_VERSION)
    set(PHOSPHORGEOMETRY_QT_MIN ${QT_MIN_VERSION})
else()
    set(PHOSPHORGEOMETRY_QT_MIN "6.10.0")
endif()

if(NOT PROJECT_VERSION)
    project(PhosphorGeometry VERSION ${PHOSPHORGEOMETRY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

add_library(PhosphorGeometry SHARED
    src/GeometryUtils.cpp
    src/DirectionalNeighbor.cpp
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

set_target_properties(PhosphorGeometry PROPERTIES
    VERSION ${PHOSPHORGEOMETRY_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorGeometry
    EXPORT PhosphorGeometryTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
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

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorGeometry" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

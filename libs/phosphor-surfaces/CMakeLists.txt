# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSurfaces — managed Wayland layer-shell surface lifecycle.
#
# Builds on PhosphorLayer to provide:
#   • Shared QQmlEngine with caller-supplied configurator callback
#   • Vulkan keep-alive surface (prevents Qt global teardown)
#   • Scope generation counter (prevents compositor rate-limiting)
#   • Content-agnostic: callers bring their own QML and C++ types
#
# Depends on Qt6::Core, Qt6::Quick, PhosphorLayer::PhosphorLayer.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSURFACES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSurfaces VERSION ${PHOSPHORSURFACES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Quick)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorSurfaces Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorsurfaces_SRCS
    src/surfacemanager.cpp
)

set(phosphorsurfaces_public_HDRS
    include/PhosphorSurfaces/PhosphorSurfaces.h
    include/PhosphorSurfaces/SurfaceManager.h
    include/PhosphorSurfaces/SurfaceManagerConfig.h
)

add_library(PhosphorSurfaces SHARED
    ${phosphorsurfaces_public_HDRS}
    ${phosphorsurfaces_SRCS}
)

add_library(PhosphorSurfaces::PhosphorSurfaces ALIAS PhosphorSurfaces)

generate_export_header(PhosphorSurfaces
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaces/phosphorsurfaces_export.h
    EXPORT_MACRO_NAME PHOSPHORSURFACES_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorSurfaces
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSurfaces PUBLIC cxx_std_20)

target_link_libraries(PhosphorSurfaces
    PUBLIC
        Qt6::Core
        Qt6::Quick
        PhosphorLayer::PhosphorLayer
)

set_target_properties(PhosphorSurfaces PROPERTIES
    VERSION ${PHOSPHORSURFACES_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorSurfaces" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorSurfaces
    EXPORT PhosphorSurfacesTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorsurfaces_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurfaces
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfaces/phosphorsurfaces_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSurfaces
)

install(EXPORT PhosphorSurfacesTargets
    FILE PhosphorSurfacesTargets.cmake
    NAMESPACE PhosphorSurfaces::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaces
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSurfacesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfacesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaces
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfacesConfigVersion.cmake"
    VERSION ${PHOSPHORSURFACES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfacesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSurfacesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSurfaces
)

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorLayer — wlr-layer-shell surface lifecycle management
#
# A Qt6/C++20 library for managing the lifecycle of layer-shell surfaces
# (overlays, panels, docks, backgrounds, modals, OSDs) with dependency
# injection, testable abstractions, and consumer-defined presets.
#
# Depends on Qt6 Core/Gui/Quick only. The default layer-shell transport
# (PhosphorShellTransport) is added in a later phase; step 1 ships only
# the abstract API + interfaces so consumers can start integrating now.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORLAYER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorLayer VERSION ${PHOSPHORLAYER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorLayer Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorlayer_SRCS
    src/role.cpp
    src/surface.cpp
    src/surfacefactory.cpp
    src/topologycoordinator.cpp
)

set(phosphorlayer_public_HDRS
    include/PhosphorLayer/PhosphorLayer.h
    include/PhosphorLayer/Role.h
    include/PhosphorLayer/SurfaceConfig.h
    include/PhosphorLayer/Surface.h
    include/PhosphorLayer/SurfaceFactory.h
    include/PhosphorLayer/ScreenSurfaceRegistry.h
    include/PhosphorLayer/TopologyCoordinator.h
    include/PhosphorLayer/IScreenProvider.h
    include/PhosphorLayer/ILayerShellTransport.h
    include/PhosphorLayer/IQmlEngineProvider.h
)

add_library(PhosphorLayer SHARED
    ${phosphorlayer_public_HDRS}
    ${phosphorlayer_SRCS}
)

add_library(PhosphorLayer::PhosphorLayer ALIAS PhosphorLayer)

# Generate the export header under the PhosphorLayer/ prefix so the same
# #include <PhosphorLayer/phosphorlayer_export.h> resolves in both in-tree
# builds (via BUILD_INTERFACE below) and installed consumers.
generate_export_header(PhosphorLayer
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayer/phosphorlayer_export.h
    EXPORT_MACRO_NAME PHOSPHORLAYER_EXPORT
)

target_include_directories(PhosphorLayer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorLayer
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

set_target_properties(PhosphorLayer PROPERTIES
    VERSION ${PHOSPHORLAYER_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(TARGETS PhosphorLayer
    EXPORT PhosphorLayerTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

# Headers (regular .h via DIRECTORY pattern, then extensionless camelcase
# forwarders via explicit FILES — FILES_MATCHING PATTERN doesn't pair well
# with non-".h" filenames).
install(DIRECTORY include/PhosphorLayer/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    include/PhosphorLayer/Role
    include/PhosphorLayer/SurfaceConfig
    include/PhosphorLayer/Surface
    include/PhosphorLayer/SurfaceFactory
    include/PhosphorLayer/ScreenSurfaceRegistry
    include/PhosphorLayer/TopologyCoordinator
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayer/phosphorlayer_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayer
)

# ═══════════════════════════════════════════════════════════════════════════════
# CMake Package Config (for find_package(PhosphorLayer))
# ═══════════════════════════════════════════════════════════════════════════════

include(CMakePackageConfigHelpers)

install(EXPORT PhosphorLayerTargets
    FILE PhosphorLayerTargets.cmake
    NAMESPACE PhosphorLayer::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorLayerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfigVersion.cmake"
    VERSION ${PHOSPHORLAYER_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayerConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayer
)

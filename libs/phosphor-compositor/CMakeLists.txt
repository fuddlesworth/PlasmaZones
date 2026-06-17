# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorCompositor — Compositor-plugin SDK for Phosphor.
#
# Provides ICompositorBridge interface, shared types (WindowInfo,
# AutotileState, FloatingCache, ZoneCache), trigger parsing, and snap-assist
# candidate filtering. Compositor plugins implement ICompositorBridge
# and talk to the Phosphor daemon over D-Bus.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORCOMPOSITOR_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorCompositor VERSION ${PHOSPHORCOMPOSITOR_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Unconditional like phosphor-identity: the in-tree superbuild has already
# found Qt (this re-find is a fast cache hit), and a standalone configure
# needs it — without these the namespaced targets in target_link_libraries
# below would fail at generate time. Config.cmake.in demands Qt 6.6 of
# consumers, so enforce the same minimum when building. The sibling
# phosphor libs are resolved via find_package only when a parent scope has
# not already defined their targets (i.e. genuine standalone configure).
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui DBus)
if(NOT TARGET PhosphorProtocol::PhosphorProtocol)
    find_package(PhosphorProtocol REQUIRED)
endif()
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity REQUIRED)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorCompositor SHARED
    include/PhosphorCompositor/ICompositorBridge.h
    include/PhosphorCompositor/IDragHandler.h
    include/PhosphorCompositor/IGeometryHandler.h
    include/PhosphorCompositor/ILifecycleHandler.h
    include/PhosphorCompositor/DaemonClient.h
    include/PhosphorCompositor/AutotileState.h
    include/PhosphorCompositor/DecorationDefaults.h
    include/PhosphorCompositor/DecorationManager.h
    include/PhosphorCompositor/FloatingCache.h
    include/PhosphorCompositor/ZoneCache.h
    include/PhosphorCompositor/DebouncedAction.h
    include/PhosphorCompositor/GeometryHelpers.h
    include/PhosphorCompositor/SnapAssistFilter.h
    include/PhosphorCompositor/TriggerParser.h
    src/DaemonClient.cpp
    src/DecorationManager.cpp
    src/SnapAssistFilter.cpp
    src/TriggerParser.cpp
)
add_library(PhosphorCompositor::PhosphorCompositor ALIAS PhosphorCompositor)

generate_export_header(PhosphorCompositor
    EXPORT_FILE_NAME phosphorcompositor_export.h
    EXPORT_MACRO_NAME PHOSPHORCOMPOSITOR_EXPORT
)

# Fallback BEFORE first use: ${KDE_INSTALL_INCLUDEDIR} is expanded into the
# $<INSTALL_INTERFACE:> below at this point, so defining it afterwards would
# export an empty install include dir in a non-KDE configure (mirrors
# phosphor-identity's ordering).
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorCompositor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorCompositor PUBLIC cxx_std_20)

target_link_libraries(PhosphorCompositor
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        PhosphorProtocol::PhosphorProtocol
        PhosphorIdentity::PhosphorIdentity
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

set_target_properties(PhosphorCompositor PROPERTIES
    SOVERSION 0
    VERSION ${PHOSPHORCOMPOSITOR_VERSION}
)

install(TARGETS PhosphorCompositor
    EXPORT PhosphorCompositorTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorCompositor
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorcompositor_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorCompositor
)

install(EXPORT PhosphorCompositorTargets
    FILE PhosphorCompositorTargets.cmake
    NAMESPACE PhosphorCompositor::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorCompositor
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorCompositorConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorCompositorConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorCompositor
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorCompositorConfigVersion.cmake"
    VERSION ${PHOSPHORCOMPOSITOR_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorCompositorConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorCompositorConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorCompositor
)

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShellPatterns: UI-pattern recipes built on PhosphorLayer::Role.
#
# Axis-2 vocabulary between protocol primitives (PhosphorLayer) and
# consumer-side application roles (e.g. PzRoles in the PlasmaZones
# daemon). Lets any Phosphor shell compose its public roles from named
# patterns (Wallpaper, Hud, Modal, Floating, Panel(Edge), Toast(Corner))
# without having to know the wlr-layer-shell wire details.
#
# Depends on Qt6::Core, Qt6::Gui (for QMargins), PhosphorLayer::PhosphorLayer.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSHELLPATTERNS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShellPatterns VERSION ${PHOSPHORSHELLPATTERNS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorShellPatterns Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshellpatterns_SRCS
    src/patterns.cpp
)

set(phosphorshellpatterns_public_HDRS
    include/PhosphorShellPatterns/PhosphorShellPatterns.h
    include/PhosphorShellPatterns/Patterns.h
)

add_library(PhosphorShellPatterns SHARED
    ${phosphorshellpatterns_public_HDRS}
    ${phosphorshellpatterns_SRCS}
)

add_library(PhosphorShellPatterns::PhosphorShellPatterns ALIAS PhosphorShellPatterns)

generate_export_header(PhosphorShellPatterns
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatterns/phosphorshellpatterns_export.h
    EXPORT_MACRO_NAME PHOSPHORSHELLPATTERNS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorShellPatterns
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorShellPatterns PUBLIC cxx_std_20)

target_link_libraries(PhosphorShellPatterns
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorLayer::PhosphorLayer
)

set_target_properties(PhosphorShellPatterns PROPERTIES
    VERSION ${PHOSPHORSHELLPATTERNS_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorShellPatterns" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorShellPatterns
    EXPORT PhosphorShellPatternsTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorshellpatterns_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShellPatterns
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatterns/phosphorshellpatterns_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShellPatterns
)

install(EXPORT PhosphorShellPatternsTargets
    FILE PhosphorShellPatternsTargets.cmake
    NAMESPACE PhosphorShellPatterns::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellPatterns
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorShellPatternsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatternsConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellPatterns
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatternsConfigVersion.cmake"
    VERSION ${PHOSPHORSHELLPATTERNS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatternsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShellPatternsConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShellPatterns
)

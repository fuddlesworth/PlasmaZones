# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorShortcuts — domain-free global shortcut library for Qt6 Wayland
#
# Pluggable backends (KGlobalAccel, XDG Portal GlobalShortcuts, D-Bus trigger),
# consumer-facing Registry with stable string ids, and a reserved extension
# point for a future standalone Phosphor WM grabber. Built standalone or as a
# subdirectory of PlasmaZones.

cmake_minimum_required(VERSION 3.16)

# PhosphorShortcuts's own version — used for SOVERSION. When split to a
# standalone repo, this becomes the project() version.
set(PHOSPHORSHORTCUTS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorShortcuts VERSION ${PHOSPHORSHORTCUTS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Options
# ═══════════════════════════════════════════════════════════════════════════════

# KGlobalAccel support is optional: when OFF (or when KF6::GlobalAccel isn't
# found) the library still builds and ships the Portal + DBusTrigger backends.
# Consumers on non-KDE desktops don't need to pull KF6 in.
option(PHOSPHORSHORTCUTS_USE_KGLOBALACCEL
    "Build the KGlobalAccel backend (requires KF6::GlobalAccel)"
    ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui DBus)

set(_phosphorshortcuts_have_kglobalaccel OFF)
if(PHOSPHORSHORTCUTS_USE_KGLOBALACCEL)
    find_package(KF6GlobalAccel QUIET)
    if(KF6GlobalAccel_FOUND)
        set(_phosphorshortcuts_have_kglobalaccel ON)
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorshortcuts_public_HDRS
    include/PhosphorShortcuts/PhosphorShortcuts.h
    include/PhosphorShortcuts/IBackend.h
    include/PhosphorShortcuts/Registry.h
    include/PhosphorShortcuts/Factory.h
)

set(phosphorshortcuts_private_HDRS
    src/backends.h
    src/shortcutslogging.h
)

set(phosphorshortcuts_SRCS
    src/registry.cpp
    src/factory.cpp
    src/dbustriggerbackend.cpp
    src/portalbackend.cpp
    src/shortcutslogging.cpp
)

if(_phosphorshortcuts_have_kglobalaccel)
    list(APPEND phosphorshortcuts_SRCS src/kglobalaccelbackend.cpp)
endif()

add_library(PhosphorShortcuts SHARED
    ${phosphorshortcuts_public_HDRS}
    ${phosphorshortcuts_private_HDRS}
    ${phosphorshortcuts_SRCS}
)

add_library(PhosphorShortcuts::PhosphorShortcuts ALIAS PhosphorShortcuts)

generate_export_header(PhosphorShortcuts
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorshortcuts_export.h
    EXPORT_MACRO_NAME PHOSPHORSHORTCUTS_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR here so INSTALL_INTERFACE can reference it.
# See PhosphorConfig/CMakeLists.txt for the rationale.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorShortcuts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(PhosphorShortcuts PUBLIC cxx_std_20)

target_link_libraries(PhosphorShortcuts
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        Qt6::DBus
)

if(_phosphorshortcuts_have_kglobalaccel)
    target_link_libraries(PhosphorShortcuts PRIVATE KF6::GlobalAccel)
    target_compile_definitions(PhosphorShortcuts PRIVATE PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL)
endif()

set_target_properties(PhosphorShortcuts PROPERTIES
    VERSION ${PHOSPHORSHORTCUTS_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorShortcuts
    EXPORT PhosphorShortcutsTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorShortcutsTargets
    FILE PhosphorShortcutsTargets.cmake
    NAMESPACE PhosphorShortcuts::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShortcuts
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorShortcutsConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShortcutsConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShortcuts
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShortcutsConfigVersion.cmake"
    VERSION ${PHOSPHORSHORTCUTS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShortcutsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorShortcutsConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorShortcuts
)

install(DIRECTORY include/PhosphorShortcuts/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShortcuts
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorshortcuts_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorShortcuts
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

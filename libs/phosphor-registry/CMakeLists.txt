# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorRegistry — generic factory-by-name registry + plugin loader
# for the phosphor-shell UI extension seams (bar widgets, control-center
# tiles, launcher providers, OSDs, desktop widgets).
#
# Phase 1.3 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORREGISTRY_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorRegistry VERSION ${PHOSPHORREGISTRY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml)
# WatchedDirectorySet + IScanStrategy live in phosphor-fsloader. The
# plugin loader hot-reload path consumes both directly.
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader CONFIG QUIET)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorregistry_public_HDRS
    include/PhosphorRegistry/PhosphorRegistry.h
    include/PhosphorRegistry/Registry.h
    include/PhosphorRegistry/RegistryNotifier.h
    include/PhosphorRegistry/IFactoryBase.h
    include/PhosphorRegistry/IBarWidgetFactory.h
    include/PhosphorRegistry/IControlCenterTileFactory.h
    include/PhosphorRegistry/ILauncherProviderFactory.h
    include/PhosphorRegistry/IOSDFactory.h
    include/PhosphorRegistry/IDesktopWidgetFactory.h
    include/PhosphorRegistry/Manifest.h
    include/PhosphorRegistry/PluginLoader.h
)

set(phosphorregistry_SRCS
    src/registrynotifier.cpp
    src/manifest.cpp
    src/pluginloader.cpp
)

add_library(PhosphorRegistry SHARED
    ${phosphorregistry_public_HDRS}
    ${phosphorregistry_SRCS}
)

add_library(PhosphorRegistry::PhosphorRegistry ALIAS PhosphorRegistry)

generate_export_header(PhosphorRegistry
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistry/phosphorregistry_export.h
    EXPORT_MACRO_NAME PHOSPHORREGISTRY_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorRegistry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorRegistry PUBLIC cxx_std_20)

target_link_libraries(PhosphorRegistry
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
    PRIVATE
        PhosphorFsLoader::PhosphorFsLoader
)

set_target_properties(PhosphorRegistry PROPERTIES
    VERSION ${PHOSPHORREGISTRY_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorRegistry
    EXPORT PhosphorRegistryTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorregistry_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRegistry
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistry/phosphorregistry_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorRegistry
)

install(EXPORT PhosphorRegistryTargets
    FILE PhosphorRegistryTargets.cmake
    NAMESPACE PhosphorRegistry::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRegistry
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorRegistryConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistryConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRegistry
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistryConfigVersion.cmake"
    VERSION ${PHOSPHORREGISTRY_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorRegistryConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorRegistry
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorRegistry" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

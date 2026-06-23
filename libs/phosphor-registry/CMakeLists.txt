# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorRegistry: generic factory-by-name registry + plugin loader
# for the phosphor-shell UI extension seams (bar widgets, control-center
# tiles, launcher providers, OSDs, desktop widgets).
#
# Phase 1.3 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORREGISTRY_VERSION "0.1.0")
# Plugin ABI version. Mirrors PhosphorRegistry::PluginAbiVersion in
# Manifest.h. Exported as a compile definition so headers can
# static_assert the two stay in sync, and consumed by test fixture
# manifests via configure_file so they bump in lockstep with the
# header constant.
set(PHOSPHOR_PLUGIN_ABI_VERSION 1)

if(NOT PROJECT_VERSION)
    project(PhosphorRegistry VERSION ${PHOSPHORREGISTRY_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # GNU extensions (-std=gnu++20) would let non-portable code creep
    # into LGPL public headers consumed by plugin authors compiling
    # with stricter toolchains. Stay strictly ISO C++20.
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml)
# WatchedDirectorySet + IScanStrategy live in phosphor-fsloader. The
# plugin loader hot-reload path consumes both directly. REQUIRED is
# load-bearing: target_link_libraries below names the imported target
# unconditionally, so a missing dependency MUST fail configure rather
# than fail link with a confusing "target not found" error. The
# `if(NOT TARGET)` guard handles the in-tree case where phosphor-
# fsloader is built as a sibling subdir and the target is already in
# scope (there the find_package call is skipped, not silenced).
if(NOT TARGET PhosphorFsLoader::PhosphorFsLoader)
    find_package(PhosphorFsLoader CONFIG REQUIRED)
endif()

# Surface the PhosphorFsLoader dependency in the top-level
# feature_summary (mirrors the WrapVulkanHeaders pattern in the project
# root CMakeLists.txt). Guarded so a standalone configure of this
# subproject (which does not include(FeatureSummary)) stays valid.
# PhosphorFsLoader is REQUIRED here because the plugin-loader hot-reload
# path consumes WatchedDirectorySet + IScanStrategy unconditionally.
if(COMMAND set_package_properties)
    set_package_properties(PhosphorFsLoader PROPERTIES
        TYPE REQUIRED
        PURPOSE "WatchedDirectorySet + IScanStrategy for phosphor-registry's plugin hot-reload path")
endif()
if(COMMAND add_feature_info)
    set(_phosphor_registry_fsloader_found FALSE)
    if(TARGET PhosphorFsLoader::PhosphorFsLoader)
        set(_phosphor_registry_fsloader_found TRUE)
    endif()
    add_feature_info("PhosphorFsLoader"
                     _phosphor_registry_fsloader_found
                     "Filesystem watcher backing phosphor-registry's plugin hot-reload")
    unset(_phosphor_registry_fsloader_found)
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
    include/PhosphorRegistry/MetadataPackLoader.h
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
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorRegistry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorRegistry PUBLIC cxx_std_20)

target_compile_definitions(PhosphorRegistry
    PUBLIC
        PHOSPHOR_PLUGIN_ABI_VERSION=${PHOSPHOR_PLUGIN_ABI_VERSION}
)

target_link_libraries(PhosphorRegistry
    PUBLIC
        Qt6::Core
        Qt6::Qml
        # PUBLIC, not PRIVATE: the header-only MetadataPackLoader<T>
        # template (consumed by the unified domain registries) includes
        # MetadataPackScanStrategy.h + WatchedDirectorySet.h in its public
        # interface, so consumers need fsloader's include dirs + link.
        # PluginLoader.cpp's use is satisfied by the same public link.
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

# Shared standalone-or-tests gate. Lives in the repo's cmake/ helpers so
# every phosphor library can drop the boilerplate. The relative path
# resolves under both an in-tree configure (root CMakeLists.txt drives
# us) and a standalone configure of this subproject (the lib's own
# project() takes effect; the helper still resolves via the two-level-
# up cmake/ dir in the repo checkout). EXISTS-guarded so a tarball
# missing the helper still builds the library itself (just without the
# tests gate).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../cmake/PhosphorLibTesting.cmake")
    include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/PhosphorLibTesting.cmake)
    phosphor_lib_enable_tests(PhosphorRegistry)
endif()

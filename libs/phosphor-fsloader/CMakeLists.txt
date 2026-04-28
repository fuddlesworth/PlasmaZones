# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorFsLoader — filesystem-backed loader scaffolding with live reload.
#
# A tiny Qt6/C++20 library providing a reusable scaffold for "scan a set
# of directories, register entries with a consumer-supplied registry,
# watch for changes, and rescan on edit". The scaffolding (watcher,
# debounce, parent-watch promotion, file-watch re-arming, rescan-during-
# rescan race guard) lives in `WatchedDirectorySet`. Consumers plug in
# their schema-specific enumeration / parsing / commit logic via
# `IScanStrategy`.
#
# One specialisation ships with the library out of the box:
#   • `DirectoryLoader` — top-level `*.json` scan with user-wins layering,
#                         dispatch to `IDirectoryLoaderSink`. Used by
#                         CurveLoader, ProfileLoader, etc.
#
# Loaders with different on-disk shapes (subdirectory layouts, non-JSON
# extensions, custom filename validation) implement `IScanStrategy`
# directly rather than extending `DirectoryLoader`.
#
# Depends on Qt6::Core only. No Gui / Qml / DBus coupling.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORFSLOADER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorFsLoader VERSION ${PHOSPHORFSLOADER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

set(phosphorfsloader_SRCS
    src/directoryloader.cpp
    src/watcheddirectoryset.cpp
)

set(phosphorfsloader_public_HDRS
    include/PhosphorFsLoader/DirectoryLoader.h
    include/PhosphorFsLoader/IDirectoryLoaderSink.h
    include/PhosphorFsLoader/IScanStrategy.h
    include/PhosphorFsLoader/JsonEnvelopeValidator.h
    include/PhosphorFsLoader/ParsedEntry.h
    include/PhosphorFsLoader/WatchedDirectorySet.h
)

add_library(PhosphorFsLoader SHARED
    ${phosphorfsloader_public_HDRS}
    ${phosphorfsloader_SRCS}
)

add_library(PhosphorFsLoader::PhosphorFsLoader ALIAS PhosphorFsLoader)

generate_export_header(PhosphorFsLoader
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoader/phosphorfsloader_export.h
    EXPORT_MACRO_NAME PHOSPHORFSLOADER_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorFsLoader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorFsLoader PUBLIC cxx_std_20)

target_link_libraries(PhosphorFsLoader
    PUBLIC
        Qt6::Core
)

set_target_properties(PhosphorFsLoader PROPERTIES
    VERSION ${PHOSPHORFSLOADER_VERSION}
    SOVERSION 0
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorFsLoader" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS PhosphorFsLoader
    EXPORT PhosphorFsLoaderTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorfsloader_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorFsLoader
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoader/phosphorfsloader_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorFsLoader
)

install(EXPORT PhosphorFsLoaderTargets
    FILE PhosphorFsLoaderTargets.cmake
    NAMESPACE PhosphorFsLoader::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorFsLoader
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorFsLoaderConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoaderConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorFsLoader
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoaderConfigVersion.cmake"
    VERSION ${PHOSPHORFSLOADER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoaderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorFsLoaderConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorFsLoader
)

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorJsonLoader — generic JSON-directory loader with live reload.
#
# A tiny Qt6/C++20 library providing a reusable scaffold for "scan a
# directory of JSON files, register them with a consumer-supplied registry,
# watch for changes, and rescan on edit". Extracted from the near-duplicate
# CurveLoader / ProfileLoader implementations in phosphor-animation so
# future loaders (shader packs, layout preset packs, etc.) don't re-invent
# the debounce / watcher / user-wins-collision / stale-entry purge logic.
#
# The library is consumer-agnostic: it knows nothing about the JSON
# schema or the target registry. Callers implement IDirectoryLoaderSink
# (parse one file → ParsedEntry; commit a full rescan batch) and the
# library drives the directory walk + watching.
#
# Depends on Qt6::Core only. No Gui / Qml / DBus coupling.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORJSONLOADER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorJsonLoader VERSION ${PHOSPHORJSONLOADER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

set(phosphorjsonloader_SRCS
    src/directoryloader.cpp
)

set(phosphorjsonloader_public_HDRS
    include/PhosphorJsonLoader/DirectoryLoader.h
    include/PhosphorJsonLoader/IDirectoryLoaderSink.h
    include/PhosphorJsonLoader/JsonEnvelopeValidator.h
    include/PhosphorJsonLoader/ParsedEntry.h
)

add_library(PhosphorJsonLoader SHARED
    ${phosphorjsonloader_public_HDRS}
    ${phosphorjsonloader_SRCS}
)

add_library(PhosphorJsonLoader::PhosphorJsonLoader ALIAS PhosphorJsonLoader)

generate_export_header(PhosphorJsonLoader
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoader/phosphorjsonloader_export.h
    EXPORT_MACRO_NAME PHOSPHORJSONLOADER_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorJsonLoader
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorJsonLoader PUBLIC cxx_std_20)

target_link_libraries(PhosphorJsonLoader
    PUBLIC
        Qt6::Core
)

set_target_properties(PhosphorJsonLoader PROPERTIES
    VERSION ${PHOSPHORJSONLOADER_VERSION}
    SOVERSION 0
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorJsonLoader" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS PhosphorJsonLoader
    EXPORT PhosphorJsonLoaderTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorjsonloader_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorJsonLoader
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoader/phosphorjsonloader_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorJsonLoader
)

install(EXPORT PhosphorJsonLoaderTargets
    FILE PhosphorJsonLoaderTargets.cmake
    NAMESPACE PhosphorJsonLoader::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorJsonLoader
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorJsonLoaderConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoaderConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorJsonLoader
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoaderConfigVersion.cmake"
    VERSION ${PHOSPHORJSONLOADER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoaderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorJsonLoaderConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorJsonLoader
)

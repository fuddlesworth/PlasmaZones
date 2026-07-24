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

# 3.18 floor: the vendored-valijson acquisition below uses file(ARCHIVE_EXTRACT),
# introduced in CMake 3.18.
cmake_minimum_required(VERSION 3.18)

set(PHOSPHORFSLOADER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorFsLoader VERSION ${PHOSPHORFSLOADER_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core)

# ═══════════════════════════════════════════════════════════════════════════════
# valijson acquisition — header-only JSON Schema (Draft 7) validator.
#
# Committed source tarball by default (offline builds, mirroring the vendored
# Luau in phosphor-scripting); system headers opt-in. valijson is header-only
# and used only inside src/schemavalidator.cpp, so it is exposed as a PRIVATE
# SYSTEM include (its warnings stay out of our -Wall/-Wextra build, and it
# never leaks into the public ABI). No add_subdirectory: we want the headers,
# not valijson's own install/export rules.
# ═══════════════════════════════════════════════════════════════════════════════

option(PHOSPHORFSLOADER_USE_SYSTEM_VALIJSON
    "Use system-provided valijson headers instead of the committed extern/valijson-*.tar.gz" OFF)

set(PHOSPHORFSLOADER_VALIJSON_VERSION "1.1.3")
set(PHOSPHORFSLOADER_VALIJSON_SHA256 "887b53b1a924f6fe0b35fa3bbc9bbbe5ae8c72097b7f0f7c17b45f4cfa646029")

if(PHOSPHORFSLOADER_USE_SYSTEM_VALIJSON)
    find_path(VALIJSON_INCLUDE_DIR
        NAMES valijson/validator.hpp
        DOC "Directory containing valijson/validator.hpp")
    if(NOT VALIJSON_INCLUDE_DIR)
        message(FATAL_ERROR
            "PHOSPHORFSLOADER_USE_SYSTEM_VALIJSON=ON but valijson headers "
            "(valijson/validator.hpp) were not found. Install valijson dev files "
            "or unset the option to use the committed vendored tarball.")
    endif()
else()
    # extern/ lives at the repository root; ../../extern resolves there whether
    # this lib is configured as a subdirectory of the main project or built
    # standalone for its own tests.
    set(_pz_valijson_tarball
        "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/valijson-${PHOSPHORFSLOADER_VALIJSON_VERSION}.tar.gz")
    if(NOT EXISTS "${_pz_valijson_tarball}")
        message(FATAL_ERROR
            "Vendored valijson tarball missing at ${_pz_valijson_tarball}. It is "
            "committed in the repository, so an incomplete checkout is the likely "
            "cause — or pass -DPHOSPHORFSLOADER_USE_SYSTEM_VALIJSON=ON to use system headers.")
    endif()
    # Verify the committed blob before extracting unknown source from it.
    file(SHA256 "${_pz_valijson_tarball}" _pz_valijson_actual_sha)
    if(NOT _pz_valijson_actual_sha STREQUAL "${PHOSPHORFSLOADER_VALIJSON_SHA256}")
        message(FATAL_ERROR
            "Vendored valijson tarball SHA256 mismatch:\n  expected ${PHOSPHORFSLOADER_VALIJSON_SHA256}\n"
            "  got      ${_pz_valijson_actual_sha}\nThe committed ${_pz_valijson_tarball} is corrupt or was swapped.")
    endif()
    # Extract once. The re-extract guard keys on a stamp written AFTER a
    # successful extraction (not on a file the archiver writes early), so an
    # aborted extract repairs itself on the next configure rather than passing
    # the guard with a half-populated tree.
    set(_pz_valijson_root "${CMAKE_CURRENT_BINARY_DIR}/_valijson")
    set(_pz_valijson_stamp "${_pz_valijson_root}/.extracted-${PHOSPHORFSLOADER_VALIJSON_VERSION}")
    if(NOT EXISTS "${_pz_valijson_stamp}")
        file(MAKE_DIRECTORY "${_pz_valijson_root}")
        file(ARCHIVE_EXTRACT INPUT "${_pz_valijson_tarball}" DESTINATION "${_pz_valijson_root}")
        file(TOUCH "${_pz_valijson_stamp}")
    endif()
    set(VALIJSON_INCLUDE_DIR
        "${_pz_valijson_root}/valijson-${PHOSPHORFSLOADER_VALIJSON_VERSION}/include")
endif()

set(phosphorfsloader_SRCS
    src/directoryloader.cpp
    src/metadatapackregistrybase.cpp
    src/metadatapackscanstrategy.cpp
    src/schemavalidator.cpp
    src/watcheddirectoryset.cpp
)

set(phosphorfsloader_public_HDRS
    include/PhosphorFsLoader/DirectoryLoader.h
    include/PhosphorFsLoader/IDirectoryLoaderSink.h
    include/PhosphorFsLoader/IScanStrategy.h
    include/PhosphorFsLoader/JsonEnvelopeValidator.h
    include/PhosphorFsLoader/MetadataPackRegistryBase.h
    include/PhosphorFsLoader/MetadataPackScanStrategy.h
    include/PhosphorFsLoader/ParsedEntry.h
    include/PhosphorFsLoader/SchemaValidator.h
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

# valijson headers are a PRIVATE implementation detail of schemavalidator.cpp.
# SYSTEM keeps the project's -Wall/-Wextra/-Wpedantic from flagging the
# third-party headers.
target_include_directories(PhosphorFsLoader SYSTEM PRIVATE "${VALIJSON_INCLUDE_DIR}")

# valijson's schema parser throws on a malformed schema; schemavalidator.cpp
# catches it to fail closed. KDECompilerSettings disables exceptions by
# default (see the -fexceptions override on vendored Luau), so re-enable them
# on just this translation unit. The VALIJSON_USE_EXCEPTIONS define (which tells
# valijson to `throw` rather than abort) is scoped to the SAME TU so the
# throwing macro and the exceptions flag always travel together — a future TU
# that includes a valijson header without this scope compiles under
# -fno-exceptions and would be unable to build the throwing code.
set_source_files_properties(src/schemavalidator.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-fexceptions>"
    COMPILE_DEFINITIONS "VALIJSON_USE_EXCEPTIONS=1")

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

if(NOT DEFINED KDE_INSTALL_DATADIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_DATADIR ${CMAKE_INSTALL_DATAROOTDIR})
endif()

# valijson is BSD-2-Clause (Tristan Penman) and, in the default vendored build,
# its headers are compiled into this library — so its licence notice must
# accompany the distribution. Ship it alongside the app's own licences, next to
# the vendored Luau notice phosphor-scripting installs. The system-valijson
# build uses separately-installed headers that carry their own licence, so skip
# it there. Guarded on the file existing for the same reason as the Luau rule:
# a scope that skips the extraction must not leave an install rule pointing at
# a path that was never created.
if(NOT PHOSPHORFSLOADER_USE_SYSTEM_VALIJSON)
    set(_pz_valijson_license
        "${_pz_valijson_root}/valijson-${PHOSPHORFSLOADER_VALIJSON_VERSION}/LICENSE")
    if(EXISTS "${_pz_valijson_license}")
        install(FILES ${_pz_valijson_license}
            DESTINATION ${KDE_INSTALL_DATADIR}/licenses/plasmazones
            RENAME LICENSE.valijson)
    endif()
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

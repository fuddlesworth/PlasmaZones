# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorConfig — declarative Qt6 configuration library
#
# Pluggable config backends (JSON, QSettings/INI), schema-driven defaults,
# and a versioned migration runner. Built standalone or as a subdirectory
# of PlasmaZones.

cmake_minimum_required(VERSION 3.16)

# PhosphorConfig's own version — used for SOVERSION. When split to a
# standalone repo, this becomes the project() version.
set(PHOSPHORCONFIG_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorConfig VERSION ${PHOSPHORCONFIG_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorconfig_public_HDRS
    include/PhosphorConfig/PhosphorConfig.h
    include/PhosphorConfig/IBackend.h
    include/PhosphorConfig/IGroupPathResolver.h
    include/PhosphorConfig/JsonBackend.h
    include/PhosphorConfig/QSettingsBackend.h
    include/PhosphorConfig/Schema.h
    include/PhosphorConfig/MigrationRunner.h
    include/PhosphorConfig/Store.h
)

set(phosphorconfig_SRCS
    src/jsonbackend.cpp
    src/qsettingsbackend.cpp
    src/migrationrunner.cpp
    src/store.cpp
)

add_library(PhosphorConfig SHARED
    ${phosphorconfig_public_HDRS}
    ${phosphorconfig_SRCS}
)

add_library(PhosphorConfig::PhosphorConfig ALIAS PhosphorConfig)

generate_export_header(PhosphorConfig
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorconfig_export.h
    EXPORT_MACRO_NAME PHOSPHORCONFIG_EXPORT
)

target_include_directories(PhosphorConfig
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(PhosphorConfig
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

set_target_properties(PhosphorConfig PROPERTIES
    VERSION ${PHOSPHORCONFIG_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Unity builds merge multiple source files into one TU, which surfaces
    # spurious "defined but not used" warnings on anonymous-namespace
    # helpers that GCC can't fully track across the merge. The translation
    # units are small enough that unity buys us nothing here.
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

install(TARGETS PhosphorConfig
    EXPORT PhosphorConfigTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorConfigTargets
    FILE PhosphorConfigTargets.cmake
    NAMESPACE PhosphorConfig::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorConfig
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorConfigConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorConfigConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorConfig
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorConfigConfigVersion.cmake"
    VERSION ${PHOSPHORCONFIG_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorConfigConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorConfigConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorConfig
)

install(DIRECTORY include/PhosphorConfig/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorConfig
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorconfig_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorConfig
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests (only when PhosphorConfig is the top-level project or BUILD_TESTING is on)
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

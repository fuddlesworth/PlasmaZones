# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorDBus — generic, service-agnostic D-Bus client utilities.
#
# Provides:
#   - Client      : a value-type method-call client bound to a
#                   (connection, service, objectPath) triple
#   - Streaming.h : the HasDBusStreaming<T> compile-time marshalling check
#
# This library knows nothing about Phosphor / PlasmaZones. Any Qt application
# talking to any D-Bus service can reuse it. Project-specific service names
# and wire types belong one layer up (see phosphor-protocol).
#
# Depends on Qt6::Core, Qt6::DBus.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORDBUS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorDBus VERSION ${PHOSPHORDBUS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core DBus)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorDBus Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphordbus_SRCS
    src/client.cpp
    src/logging.cpp
)

set(phosphordbus_public_HDRS
    include/PhosphorDBus/Client.h
    include/PhosphorDBus/Logging.h
    include/PhosphorDBus/Streaming.h
)

add_library(PhosphorDBus SHARED
    ${phosphordbus_public_HDRS}
    ${phosphordbus_SRCS}
)

add_library(PhosphorDBus::PhosphorDBus ALIAS PhosphorDBus)

generate_export_header(PhosphorDBus
    BASE_NAME PhosphorDBus
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBus/phosphordbus_export.h
    EXPORT_MACRO_NAME PHOSPHORDBUS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorDBus
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorDBus PUBLIC cxx_std_20)

target_link_libraries(PhosphorDBus
    PUBLIC
        Qt6::Core
        Qt6::DBus
)

set_target_properties(PhosphorDBus PROPERTIES
    VERSION ${PHOSPHORDBUS_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorDBus" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorDBus
    EXPORT PhosphorDBusTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphordbus_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorDBus
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBus/phosphordbus_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorDBus
)

install(EXPORT PhosphorDBusTargets
    FILE PhosphorDBusTargets.cmake
    NAMESPACE PhosphorDBus::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorDBus
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorDBusConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBusConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorDBus
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBusConfigVersion.cmake"
    VERSION ${PHOSPHORDBUS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBusConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorDBusConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorDBus
)

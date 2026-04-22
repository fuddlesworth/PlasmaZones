# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorProtocol — D-Bus wire types and service constants for Phosphor IPC.
#
# Provides:
#   - Wire-format structs for D-Bus marshalling/demarshalling
#   - Service name and path constants
#   - Client-side helper utilities
#
# Depends on Qt6::Core, Qt6::DBus.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPROTOCOL_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorProtocol VERSION ${PHOSPHORPROTOCOL_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core DBus)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorProtocol Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorprotocol_SRCS
    src/wiretypes.cpp
    src/logging.cpp
)

set(phosphorprotocol_public_HDRS
    include/PhosphorProtocol/PhosphorProtocol.h
    include/PhosphorProtocol/WireTypes.h
    include/PhosphorProtocol/ServiceConstants.h
    include/PhosphorProtocol/ClientHelpers.h
)

add_library(PhosphorProtocol SHARED
    ${phosphorprotocol_public_HDRS}
    ${phosphorprotocol_SRCS}
)

add_library(PhosphorProtocol::PhosphorProtocol ALIAS PhosphorProtocol)

generate_export_header(PhosphorProtocol
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocol/phosphorprotocol_export.h
    EXPORT_MACRO_NAME PHOSPHORPROTOCOL_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorProtocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorProtocol PUBLIC cxx_std_20)

target_link_libraries(PhosphorProtocol
    PUBLIC
        Qt6::Core
        Qt6::DBus
)

set_target_properties(PhosphorProtocol PROPERTIES
    VERSION ${PHOSPHORPROTOCOL_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorProtocol" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorProtocol
    EXPORT PhosphorProtocolTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorprotocol_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorProtocol
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocol/phosphorprotocol_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorProtocol
)

install(EXPORT PhosphorProtocolTargets
    FILE PhosphorProtocolTargets.cmake
    NAMESPACE PhosphorProtocol::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorProtocol
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorProtocolConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocolConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorProtocol
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocolConfigVersion.cmake"
    VERSION ${PHOSPHORPROTOCOL_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocolConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocolConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorProtocol
)

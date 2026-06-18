# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorProtocol — D-Bus wire types and service constants for Phosphor IPC.
#
# Two targets, layered so the value vocabulary does not drag QtDBus into
# pure-compute consumers:
#
#   PhosphorProtocol::Types  — value structs/enums. Depends only on Qt6::Core.
#                              Domain libraries (snap engine, placement) link
#                              this to name protocol results without QtDBus.
#   PhosphorProtocol::PhosphorProtocol
#                            — D-Bus marshalling, metatype registration,
#                              service constants, client helpers. Depends on
#                              ::Types + Qt6::DBus. Linked by adaptors and the
#                              daemon/effect entry points.

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

find_package(Qt6 6.10 REQUIRED COMPONENTS Core DBus)

# PhosphorDBus provides the generic D-Bus client / streaming utilities the
# marshalling + client-helper layer is built on. Available as an in-tree
# target during the umbrella build; resolved as a package otherwise.
if(NOT TARGET PhosphorDBus::PhosphorDBus)
    find_package(PhosphorDBus CONFIG REQUIRED)
endif()

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorProtocol::Types — QtCore-only value vocabulary
# ═══════════════════════════════════════════════════════════════════════════════

# Per-interface value-type headers + the QtCore-only service constants. A
# consumer recompiles only when the interface group it uses changes.
set(phosphorprotocol_types_HDRS
    include/PhosphorProtocol/ZoneTypes.h
    include/PhosphorProtocol/WindowTypes.h
    include/PhosphorProtocol/AutotileTypes.h
    include/PhosphorProtocol/NavigationTypes.h
    include/PhosphorProtocol/DragTypes.h
    include/PhosphorProtocol/BridgeTypes.h
    include/PhosphorProtocol/ServiceConstants.h
)

add_library(PhosphorProtocolTypes SHARED
    ${phosphorprotocol_types_HDRS}
    src/types.cpp
)

add_library(PhosphorProtocol::Types ALIAS PhosphorProtocolTypes)

generate_export_header(PhosphorProtocolTypes
    BASE_NAME PhosphorProtocolTypes
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocol/phosphorprotocoltypes_export.h
    EXPORT_MACRO_NAME PHOSPHORPROTOCOLTYPES_EXPORT
)

target_include_directories(PhosphorProtocolTypes
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorProtocolTypes PUBLIC cxx_std_20)

target_link_libraries(PhosphorProtocolTypes
    PUBLIC
        Qt6::Core
)

set_target_properties(PhosphorProtocolTypes PROPERTIES
    VERSION ${PHOSPHORPROTOCOL_VERSION}
    SOVERSION 0
)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorProtocol — D-Bus marshalling + service constants + client helpers
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorprotocol_SRCS
    src/marshalling.cpp
    src/logging.cpp
)

# Per-interface D-Bus marshalling headers + registration + client helpers.
set(phosphorprotocol_public_HDRS
    include/PhosphorProtocol/PhosphorProtocol.h
    include/PhosphorProtocol/ZoneMarshalling.h
    include/PhosphorProtocol/WindowMarshalling.h
    include/PhosphorProtocol/AutotileMarshalling.h
    include/PhosphorProtocol/NavigationMarshalling.h
    include/PhosphorProtocol/DragMarshalling.h
    include/PhosphorProtocol/BridgeMarshalling.h
    include/PhosphorProtocol/Registration.h
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

target_include_directories(PhosphorProtocol
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorProtocol PUBLIC cxx_std_20)

target_link_libraries(PhosphorProtocol
    PUBLIC
        PhosphorProtocol::Types
        PhosphorDBus::PhosphorDBus
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

install(TARGETS PhosphorProtocolTypes PhosphorProtocol
    EXPORT PhosphorProtocolTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES
    ${phosphorprotocol_types_HDRS}
    ${phosphorprotocol_public_HDRS}
    ${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocol/phosphorprotocoltypes_export.h
    ${CMAKE_CURRENT_BINARY_DIR}/PhosphorProtocol/phosphorprotocol_export.h
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

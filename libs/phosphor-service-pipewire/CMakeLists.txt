# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServicePipeWire: native PipeWire mixer (sinks / sources / streams,
# default-node switching, per-app volume + mute) for Phosphor-based desktop
# shells. Phase 2.1 of docs/phosphor-shell-design/04-implementation-plan.md.
#
# Single-process by design (U3): the PipeWire main loop runs on a dedicated
# QThread inside the same QGuiApplication that hosts the shell. Public API
# is libpipewire-free (pimpl); consumers only need Qt deps. WirePlumber 0.5
# powers default-node switching (added in milestone 6, not here in M1/M2).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEPIPEWIRE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServicePipeWire VERSION ${PHOSPHORSERVICEPIPEWIRE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════
#
# Qt: Core + Qml only. Qt::Gui is deliberately NOT linked (the lib is
# non-visual; mixer state is QML-bindable but rendering happens in
# consumers). Mirrors the phosphor-service-upower / mpris dependency
# discipline.
#
# PipeWire: PRIVATE link via pkg-config. U1 resolution pinned the floor
# at libpipewire-0.3 ABI 1.0+ (Arch / Fedora 38+ / Ubuntu 24.04
# baseline). All libpipewire types live in src/ behind the pimpl; the
# public headers stay libpipewire-free so consumers don't need to find
# it transitively.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.0.0)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicepipewire_public_HDRS
    include/PhosphorServicePipeWire/PhosphorServicePipeWire.h
    include/PhosphorServicePipeWire/QmlRegistration.h
    include/PhosphorServicePipeWire/PipeWireConnection.h
    include/PhosphorServicePipeWire/PwNode.h
    include/PhosphorServicePipeWire/PwNodeModel.h
)

set(phosphorservicepipewire_SRCS
    src/qmlregistration.cpp
    src/pipewireconnection.cpp
    src/pwnode.cpp
    src/pwnodemodel.cpp
)

add_library(PhosphorServicePipeWire SHARED
    ${phosphorservicepipewire_public_HDRS}
    ${phosphorservicepipewire_SRCS}
)

add_library(PhosphorServicePipeWire::PhosphorServicePipeWire ALIAS PhosphorServicePipeWire)

generate_export_header(PhosphorServicePipeWire
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWire/phosphorservicepipewire_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEPIPEWIRE_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServicePipeWire
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServicePipeWire PUBLIC cxx_std_20)

target_link_libraries(PhosphorServicePipeWire
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        PkgConfig::PIPEWIRE
)

set_target_properties(PhosphorServicePipeWire PROPERTIES
    VERSION ${PHOSPHORSERVICEPIPEWIRE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServicePipeWire
    EXPORT PhosphorServicePipeWireTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicepipewire_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServicePipeWire
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWire/phosphorservicepipewire_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServicePipeWire
)

install(EXPORT PhosphorServicePipeWireTargets
    FILE PhosphorServicePipeWireTargets.cmake
    NAMESPACE PhosphorServicePipeWire::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePipeWire
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServicePipeWireConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWireConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePipeWire
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWireConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEPIPEWIRE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWireConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePipeWireConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePipeWire
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServicePipeWire" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

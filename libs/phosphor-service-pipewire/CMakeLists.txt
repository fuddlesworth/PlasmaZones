# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServicePipeWire: native PipeWire mixer (sinks / sources / streams,
# default-node switching, per-app volume + mute) for Phosphor-based desktop
# shells. Phase 2.1 of docs/phosphor-shell-design/04-implementation-plan.md.
#
# Single-process by design (U3): the PipeWire main loop runs on a dedicated
# QThread inside the same QGuiApplication that hosts the shell. Public API
# is libpipewire-free (pimpl); consumers only need Qt deps. See the README
# "Status" section for the milestone-by-milestone landing record.

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

# Surface the PipeWire dependency in the top-level feature_summary
# (mirrors the WrapVulkanHeaders pattern in the project root
# CMakeLists.txt). add_feature_info is the FeatureSummary entry point
# for pkg-config-discovered modules; set_package_properties only applies
# to find_package targets. Guarded so a standalone configure of this
# subproject (which does not include(FeatureSummary)) stays valid.
if(COMMAND add_feature_info)
    add_feature_info("PipeWire"
                     PIPEWIRE_FOUND
                     "Native PipeWire mixer (sinks / sources / streams) for phosphor-service-pipewire")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicepipewire_public_HDRS
    include/PhosphorServicePipeWire/PhosphorServicePipeWire.h
    include/PhosphorServicePipeWire/QmlRegistration.h
    include/PhosphorServicePipeWire/PipeWireConnection.h
    include/PhosphorServicePipeWire/PipeWireHost.h
    include/PhosphorServicePipeWire/PwNode.h
    include/PhosphorServicePipeWire/PwNodeModel.h
)

set(phosphorservicepipewire_SRCS
    src/qmlregistration.cpp
    src/pipewireconnection.cpp
    src/pipewireconnection_lifecycle.cpp
    src/pipewireconnection_registry.cpp
    src/pipewireconnection_writes.cpp
    src/pipewirehost.cpp
    src/pwnode.cpp
    src/pwnodemodel.cpp
)

# Private header carrying the PipeWireConnection::Private declaration
# (split out so pipewireconnection.cpp + pipewireconnection_lifecycle.cpp
# + pipewireconnection_registry.cpp + pipewireconnection_writes.cpp can
# all implement Private methods without re-declaring it). Not installed;
# consumers see only the libpipewire-free public headers.
set(phosphorservicepipewire_PRIVATE_HDRS
    src/pipewireconnection_p.h
)

add_library(PhosphorServicePipeWire SHARED
    ${phosphorservicepipewire_public_HDRS}
    ${phosphorservicepipewire_PRIVATE_HDRS}
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
    phosphor_lib_enable_tests(PhosphorServicePipeWire)
endif()

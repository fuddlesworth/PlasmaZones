# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorPopout, centralized popout coordinator for Phosphor shells.
#
# Owns the single arbiter for popout lifetime, focus, and exclusivity
# policy across the shell. The transport seam decouples arbitration from
# the actual layer-shell surface stack so tests can run pure logic and
# alternate hosts can swap the backend.
#
# Phase 1.2 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORPOPOUT_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorPopout VERSION ${PHOSPHORPOPOUT_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorpopout_public_HDRS
    include/PhosphorPopout/PhosphorPopout.h
    include/PhosphorPopout/PopoutRequest.h
    include/PhosphorPopout/IPopoutTransport.h
    include/PhosphorPopout/IPopoutService.h
    include/PhosphorPopout/PopoutController.h
)

set(phosphorpopout_SRCS
    src/popoutcontroller.cpp
)

add_library(PhosphorPopout SHARED
    ${phosphorpopout_public_HDRS}
    ${phosphorpopout_SRCS}
)

add_library(PhosphorPopout::PhosphorPopout ALIAS PhosphorPopout)

generate_export_header(PhosphorPopout
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopout/phosphorpopout_export.h
    EXPORT_MACRO_NAME PHOSPHORPOPOUT_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

target_include_directories(PhosphorPopout
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorPopout PUBLIC cxx_std_20)

target_link_libraries(PhosphorPopout
    PUBLIC
        Qt6::Core
        Qt6::Gui
        # PopoutController carries QML_ELEMENT from
        # <QtQmlIntegration/qqmlintegration.h>. The macro expands to
        # markers that AUTOMOC and qmltyperegistrar pick up at build
        # time; the header itself is part of Qt6::Qml.
        Qt6::Qml
)

set_target_properties(PhosphorPopout PROPERTIES
    VERSION ${PHOSPHORPOPOUT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_BINDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorPopout
    EXPORT PhosphorPopoutTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(FILES ${phosphorpopout_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPopout
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopout/phosphorpopout_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorPopout
)

install(EXPORT PhosphorPopoutTargets
    FILE PhosphorPopoutTargets.cmake
    NAMESPACE PhosphorPopout::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorPopoutConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfigVersion.cmake"
    VERSION ${PHOSPHORPOPOUT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorPopoutConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorPopout
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorPopout" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

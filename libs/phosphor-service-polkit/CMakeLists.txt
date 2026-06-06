# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServicePolkit: a PolicyKit authentication agent for Phosphor-based
# desktop shells, via the polkit-qt6 binding. Phase 2.6 of the service-library
# split (see docs/phosphor-shell-design/04-implementation-plan.md). polkitd
# calls into the registered agent when an app requests a privileged action, and
# the agent drives the PAM conversation that authenticates the user. polkit-qt6
# is wrapped privately, so it does not appear in the public link interface.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEPOLKIT_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServicePolkit VERSION ${PHOSPHORSERVICEPOLKIT_VERSION} LANGUAGES CXX)
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
# Core / Qml: the QObject + QML surface. PolkitQt6-1 (Core + Agent) is the
# backend; it is PRIVATE because the polkit-qt types are wrapped inside the .cpp
# (the public PolkitAgent header is a plain QObject with no polkit-qt types), so
# consumers neither include nor link polkit-qt. No Qt Gui type is used, and
# PolkitQt6-1::Agent does not pull Qt6::Gui transitively, so Gui is not a
# dependency. The polkit-qt6 GUI action-button widgets (PolkitQt6-1::Gui) are a
# Phase 3/4 UI concern and are NOT linked.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml)
find_package(PolkitQt6-1 REQUIRED)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicepolkit_public_HDRS
    include/PhosphorServicePolkit/PhosphorServicePolkit.h
    include/PhosphorServicePolkit/QmlRegistration.h
    include/PhosphorServicePolkit/PolkitAgent.h
    include/PhosphorServicePolkit/AuthRequest.h
)

set(phosphorservicepolkit_SRCS
    src/qmlregistration.cpp
    src/polkitagent.cpp
    src/authrequest.cpp
)

add_library(PhosphorServicePolkit SHARED
    ${phosphorservicepolkit_public_HDRS}
    ${phosphorservicepolkit_SRCS}
)

add_library(PhosphorServicePolkit::PhosphorServicePolkit ALIAS PhosphorServicePolkit)

generate_export_header(PhosphorServicePolkit
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkit/phosphorservicepolkit_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEPOLKIT_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServicePolkit
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServicePolkit PUBLIC cxx_std_20)

target_link_libraries(PhosphorServicePolkit
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        PolkitQt6-1::Core
        PolkitQt6-1::Agent
)

set_target_properties(PhosphorServicePolkit PROPERTIES
    VERSION ${PHOSPHORSERVICEPOLKIT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServicePolkit
    EXPORT PhosphorServicePolkitTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicepolkit_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServicePolkit
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkit/phosphorservicepolkit_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServicePolkit
)

install(EXPORT PhosphorServicePolkitTargets
    FILE PhosphorServicePolkitTargets.cmake
    NAMESPACE PhosphorServicePolkit::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePolkit
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServicePolkitConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkitConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePolkit
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkitConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEPOLKIT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkitConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServicePolkitConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServicePolkit
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServicePolkit" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

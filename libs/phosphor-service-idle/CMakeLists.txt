# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceIdle: a Wayland idle-management service for Phosphor-based
# desktop shells. Phase 2.7 of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). It watches the session
# for inactivity through a configurable multi-stage timeout policy and inhibits
# idle on request. It is the policy layer over the raw protocol clients that
# already live in phosphor-wayland (IdleNotifier / IdleInhibitor), so it does
# not re-vendor protocols; phosphor-wayland is wrapped privately and does not
# appear in the public link interface.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEIDLE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceIdle VERSION ${PHOSPHORSERVICEIDLE_VERSION} LANGUAGES CXX)
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
# Core / Qml: the QObject + QML surface. PhosphorWayland is the backend; it is
# PRIVATE because the foundation idle clients (IdleNotifier / IdleInhibitor) are
# used inside the .cpp and never appear in the public headers, so consumers
# neither include nor link phosphor-wayland.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml)

# phosphor-wayland is an in-tree sibling under BUILD_PHOSPHOR_SHELL; only
# find_package it when building this lib stand-alone.
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland REQUIRED)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorserviceidle_public_HDRS
    include/PhosphorServiceIdle/PhosphorServiceIdle.h
    include/PhosphorServiceIdle/QmlRegistration.h
    include/PhosphorServiceIdle/IdleService.h
)

set(phosphorserviceidle_SRCS
    src/qmlregistration.cpp
    src/idleservice.cpp
    src/iidlesource.cpp
    src/idlestatemachine.cpp
    src/idlenotifiersource.cpp
    src/idleinhibitionmanager.cpp
)

add_library(PhosphorServiceIdle SHARED
    ${phosphorserviceidle_public_HDRS}
    ${phosphorserviceidle_SRCS}
)

add_library(PhosphorServiceIdle::PhosphorServiceIdle ALIAS PhosphorServiceIdle)

generate_export_header(PhosphorServiceIdle
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdle/phosphorserviceidle_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEIDLE_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceIdle
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceIdle PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceIdle
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        PhosphorWayland::PhosphorWayland
)

set_target_properties(PhosphorServiceIdle PROPERTIES
    VERSION ${PHOSPHORSERVICEIDLE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceIdle
    EXPORT PhosphorServiceIdleTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorserviceidle_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceIdle
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdle/phosphorserviceidle_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceIdle
)

install(EXPORT PhosphorServiceIdleTargets
    FILE PhosphorServiceIdleTargets.cmake
    NAMESPACE PhosphorServiceIdle::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIdle
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceIdleConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdleConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIdle
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdleConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEIDLE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIdleConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIdle
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceIdle" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

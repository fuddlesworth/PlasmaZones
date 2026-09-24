# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceSession: logind session manager for Phosphor-based desktop
# shells. Phase 2.10 of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). Surfaces the
# session/power actions (lock, logout, suspend, hibernate, reboot, power-off,
# ...) over org.freedesktop.login1.Manager, manages logind inhibitor locks
# (delay-on-sleep for lock-before-suspend, block-on-keys for shell key
# ownership), and surfaces logind's session + sleep signals. Pure D-Bus on the
# system bus, no ObjectManager (single fixed Manager object). Mirrors the
# Phase-2.4 brightness sibling pattern.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICESESSION_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceSession VERSION ${PHOSPHORSERVICESESSION_VERSION} LANGUAGES CXX)
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
# Core / Qml: the QObject + QML surface. DBus: SessionHost's injectable
# (connection, service) ctor exposes QDBusConnection in the public header, so
# DBus is PUBLIC. Qt::Gui is deliberately NOT linked (session actions are
# non-visual). PhosphorDBus is PRIVATE: only the .cpp uses PhosphorDBus::Client
# for the logind calls.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicesession_public_HDRS
    include/PhosphorServiceSession/PhosphorServiceSession.h
    include/PhosphorServiceSession/QmlRegistration.h
    include/PhosphorServiceSession/SessionHost.h
)

set(phosphorservicesession_SRCS
    src/qmlregistration.cpp
    src/sessionhost.cpp
)

add_library(PhosphorServiceSession SHARED
    ${phosphorservicesession_public_HDRS}
    ${phosphorservicesession_SRCS}
)

add_library(PhosphorServiceSession::PhosphorServiceSession ALIAS PhosphorServiceSession)

generate_export_header(PhosphorServiceSession
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSession/phosphorservicesession_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICESESSION_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceSession
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceSession PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceSession
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
    PRIVATE
        PhosphorDBus::PhosphorDBus
)

set_target_properties(PhosphorServiceSession PROPERTIES
    VERSION ${PHOSPHORSERVICESESSION_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceSession
    EXPORT PhosphorServiceSessionTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicesession_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceSession
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSession/phosphorservicesession_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceSession
)

install(EXPORT PhosphorServiceSessionTargets
    FILE PhosphorServiceSessionTargets.cmake
    NAMESPACE PhosphorServiceSession::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSession
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceSessionConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSessionConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSession
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSessionConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICESESSION_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSessionConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceSessionConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceSession
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceSession" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

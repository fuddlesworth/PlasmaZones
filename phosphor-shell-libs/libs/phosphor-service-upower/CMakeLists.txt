# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceUPower: UPower (org.freedesktop.UPower) battery /
# power-supply readouts for Phosphor-based desktop shells. Extracted
# from the original `phosphor-services` umbrella as the first of the
# Phase 2.0 tenant splits (see docs/phosphor-shell-design/04-
# implementation-plan.md). Pure D-Bus client, no UI.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEUPOWER_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceUPower VERSION ${PHOSPHORSERVICEUPOWER_VERSION} LANGUAGES CXX)
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
# Core / DBus: the C++ surface is QObject + QDBusConnection. Qml: the
# UPowerHost / UPowerDeviceModel / UPowerDevice types are exposed
# through a hand-rolled `qmlRegisterType` block in src/qmlregistration.cpp
# (no `qt_add_qml_module()`, so no qmltyperegistrar TU is generated).
# Qt::Gui is deliberately NOT linked: nothing in the lib uses QImage /
# QColor / QGuiApplication, and consumers that put these types into a
# QQmlEngine already drag in Qt::Gui themselves through their
# application infrastructure. Keeping the lib Gui-free lets CLI
# consumers (phosphorctl-style demos) link it without inheriting the
# full GUI stack.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorserviceupower_public_HDRS
    include/PhosphorServiceUPower/PhosphorServiceUPower.h
    include/PhosphorServiceUPower/QmlRegistration.h
    include/PhosphorServiceUPower/UPowerDevice.h
    include/PhosphorServiceUPower/UPowerHost.h
    include/PhosphorServiceUPower/UPowerDeviceModel.h
)

set(phosphorserviceupower_SRCS
    src/qmlregistration.cpp
    src/upowerdevice.cpp
    src/upowerhost.cpp
    src/upowerdevicemodel.cpp
)

add_library(PhosphorServiceUPower SHARED
    ${phosphorserviceupower_public_HDRS}
    ${phosphorserviceupower_SRCS}
)

add_library(PhosphorServiceUPower::PhosphorServiceUPower ALIAS PhosphorServiceUPower)

generate_export_header(PhosphorServiceUPower
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPower/phosphorserviceupower_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEUPOWER_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceUPower
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceUPower PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceUPower
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
)

set_target_properties(PhosphorServiceUPower PROPERTIES
    VERSION ${PHOSPHORSERVICEUPOWER_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceUPower
    EXPORT PhosphorServiceUPowerTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorserviceupower_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceUPower
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPower/phosphorserviceupower_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceUPower
)

install(EXPORT PhosphorServiceUPowerTargets
    FILE PhosphorServiceUPowerTargets.cmake
    NAMESPACE PhosphorServiceUPower::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceUPower
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceUPowerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPowerConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceUPower
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPowerConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEUPOWER_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPowerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceUPowerConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceUPower
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceUPower" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

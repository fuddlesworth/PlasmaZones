# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceBluetooth: BlueZ (org.bluez) adapter / device readouts and
# the pairing agent for Phosphor-based desktop shells. Phase 2.3 of the
# service-library split (see docs/phosphor-shell-design/04-implementation-
# plan.md). Pure D-Bus client, no UI. Built on PhosphorDBus::Client and
# PhosphorDBus::ObjectManager (BlueZ is ObjectManager-rooted), mirroring the
# Phase-2.1/2.2 sibling pattern (phosphor-service-pipewire / -network).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEBLUETOOTH_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceBluetooth VERSION ${PHOSPHORSERVICEBLUETOOTH_VERSION} LANGUAGES CXX)
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
# Core / DBus: the C++ surface is QObject + QDBusConnection. Qml: the host /
# model / domain types are exposed through a hand-rolled `qmlRegisterType`
# block in src/qmlregistration.cpp (no `qt_add_qml_module()`). Qt::Gui is
# deliberately NOT linked: nothing in the lib uses QImage / QColor /
# QGuiApplication, keeping CLI consumers free of the GUI stack. PhosphorDBus
# is PRIVATE: only the .cpp uses PhosphorDBus::Client / ::ObjectManager; the
# public headers stay free of it.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicebluetooth_public_HDRS
    include/PhosphorServiceBluetooth/PhosphorServiceBluetooth.h
    include/PhosphorServiceBluetooth/QmlRegistration.h
    include/PhosphorServiceBluetooth/BluetoothAdapter.h
    include/PhosphorServiceBluetooth/BluetoothAdapterModel.h
    include/PhosphorServiceBluetooth/BluetoothAgent.h
    include/PhosphorServiceBluetooth/BluetoothDevice.h
    include/PhosphorServiceBluetooth/BluetoothDeviceModel.h
    include/PhosphorServiceBluetooth/BluetoothHost.h
)

set(phosphorservicebluetooth_SRCS
    src/qmlregistration.cpp
    src/bluetoothadapter.cpp
    src/bluetoothadaptermodel.cpp
    src/bluetoothagent.cpp
    src/bluetoothdevice.cpp
    src/bluetoothdevicemodel.cpp
    src/bluetoothhost.cpp
)

add_library(PhosphorServiceBluetooth SHARED
    ${phosphorservicebluetooth_public_HDRS}
    ${phosphorservicebluetooth_SRCS}
)

add_library(PhosphorServiceBluetooth::PhosphorServiceBluetooth ALIAS PhosphorServiceBluetooth)

generate_export_header(PhosphorServiceBluetooth
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetooth/phosphorservicebluetooth_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEBLUETOOTH_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceBluetooth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceBluetooth PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceBluetooth
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
    PRIVATE
        PhosphorDBus::PhosphorDBus
)

set_target_properties(PhosphorServiceBluetooth PROPERTIES
    VERSION ${PHOSPHORSERVICEBLUETOOTH_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceBluetooth
    EXPORT PhosphorServiceBluetoothTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicebluetooth_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceBluetooth
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetooth/phosphorservicebluetooth_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceBluetooth
)

install(EXPORT PhosphorServiceBluetoothTargets
    FILE PhosphorServiceBluetoothTargets.cmake
    NAMESPACE PhosphorServiceBluetooth::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBluetooth
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceBluetoothConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetoothConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBluetooth
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetoothConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEBLUETOOTH_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetoothConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBluetoothConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBluetooth
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceBluetooth" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

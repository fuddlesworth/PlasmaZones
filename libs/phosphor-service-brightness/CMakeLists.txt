# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceBrightness: display + keyboard backlight brightness for
# Phosphor-based desktop shells. Phase 2.4 of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). Read path is sysfs
# (/sys/class/backlight + /sys/class/leds/*::kbd_backlight); the privileged
# write path is logind's org.freedesktop.login1.Session.SetBrightness (no root
# / udev needed). Mirrors the Phase-2.2/2.3 sibling pattern.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEBRIGHTNESS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceBrightness VERSION ${PHOSPHORSERVICEBRIGHTNESS_VERSION} LANGUAGES CXX)
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
# Core / Qml: the QObject + QML surface. DBus: BrightnessHost's injectable
# (connection, service) ctor exposes QDBusConnection in the public header, so
# DBus is PUBLIC. Qt::Gui is deliberately NOT linked. PhosphorDBus is PRIVATE:
# only the .cpp uses PhosphorDBus::Client for the logind SetBrightness call.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Qml DBus)

# Optional: libddcutil for external-monitor brightness over DDC/CI. When absent,
# the library still builds and simply surfaces no external displays (the sysfs
# backlight + keyboard path is unaffected). Detected via pkg-config.
find_package(PkgConfig QUIET)
set(PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(Ddcutil IMPORTED_TARGET ddcutil)
    if(Ddcutil_FOUND)
        set(PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL ON)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorservicebrightness_public_HDRS
    include/PhosphorServiceBrightness/PhosphorServiceBrightness.h
    include/PhosphorServiceBrightness/QmlRegistration.h
    include/PhosphorServiceBrightness/BrightnessDevice.h
    include/PhosphorServiceBrightness/BrightnessDeviceModel.h
    include/PhosphorServiceBrightness/BrightnessHost.h
)

set(phosphorservicebrightness_SRCS
    src/qmlregistration.cpp
    src/brightnessdevice.cpp
    src/brightnessdevicemodel.cpp
    src/brightnesshost.cpp
)

add_library(PhosphorServiceBrightness SHARED
    ${phosphorservicebrightness_public_HDRS}
    ${phosphorservicebrightness_SRCS}
)

add_library(PhosphorServiceBrightness::PhosphorServiceBrightness ALIAS PhosphorServiceBrightness)

generate_export_header(PhosphorServiceBrightness
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightness/phosphorservicebrightness_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEBRIGHTNESS_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceBrightness
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceBrightness PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceBrightness
    PUBLIC
        Qt6::Core
        Qt6::Qml
        Qt6::DBus
    PRIVATE
        PhosphorDBus::PhosphorDBus
)

set_target_properties(PhosphorServiceBrightness PROPERTIES
    VERSION ${PHOSPHORSERVICEBRIGHTNESS_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# External-monitor (DDC/CI) support is optional: only compiled when libddcutil
# is present. The DdcController source + the gate macro + the link are all
# guarded, so the library builds identically without ddcutil (just surfacing no
# external displays). The header (src/ddccontroller.h) stays private.
if(PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL)
    target_sources(PhosphorServiceBrightness PRIVATE src/ddccontroller.cpp)
    target_compile_definitions(PhosphorServiceBrightness PRIVATE PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL)
    target_link_libraries(PhosphorServiceBrightness PRIVATE PkgConfig::Ddcutil)
    message(STATUS "PhosphorServiceBrightness: DDC/CI external-display support enabled (libddcutil)")
else()
    message(STATUS "PhosphorServiceBrightness: libddcutil not found, external-display (DDC/CI) support disabled")
endif()

# Surface the optional libddcutil dependency in the top-level feature_summary
# (the project calls feature_summary() in the root CMakeLists.txt).
# add_feature_info is the FeatureSummary entry point for pkg-config-discovered
# modules; mirrors the phosphor-service-pipewire convention.
if(COMMAND add_feature_info)
    add_feature_info("Ddcutil"
        PHOSPHORSERVICEBRIGHTNESS_HAVE_DDCUTIL
        "External-display brightness over DDC/CI for phosphor-service-brightness"
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceBrightness
    EXPORT PhosphorServiceBrightnessTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorservicebrightness_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceBrightness
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightness/phosphorservicebrightness_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceBrightness
)

install(EXPORT PhosphorServiceBrightnessTargets
    FILE PhosphorServiceBrightnessTargets.cmake
    NAMESPACE PhosphorServiceBrightness::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBrightness
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceBrightnessConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightnessConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBrightness
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightnessConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEBRIGHTNESS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightnessConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceBrightnessConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceBrightness
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceBrightness" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceIconTheme: XDG icon-theme spec 0.13 resolver and
# Qt image provider for Phosphor-based desktop shells. Extracted
# from the original `phosphor-services` umbrella as part of the
# Phase 2.0 tenant splits documented in
# docs/phosphor-shell-design/04-implementation-plan.md.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICEICONTHEME_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceIconTheme VERSION ${PHOSPHORSERVICEICONTHEME_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# Gui: QImage / QIcon. Qml + Quick: QQuickImageProvider + the
# QQmlEngine plumbing inside installImageProvider. No D-Bus: the
# resolver walks the local filesystem only; tray icons that arrive
# over D-Bus are decoded with the in-class decodePixmaps helper, no
# bus call.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(phosphorserviceicontheme_public_HDRS
    include/PhosphorServiceIconTheme/PhosphorServiceIconTheme.h
    include/PhosphorServiceIconTheme/QmlRegistration.h
    include/PhosphorServiceIconTheme/IconThemeResolver.h
    include/PhosphorServiceIconTheme/IconImageProvider.h
)

set(phosphorserviceicontheme_SRCS
    src/qmlregistration.cpp
    src/iconthemeresolver.cpp
    src/iconimageprovider.cpp
)

add_library(PhosphorServiceIconTheme SHARED
    ${phosphorserviceicontheme_public_HDRS}
    ${phosphorserviceicontheme_SRCS}
)

add_library(PhosphorServiceIconTheme::PhosphorServiceIconTheme ALIAS PhosphorServiceIconTheme)

generate_export_header(PhosphorServiceIconTheme
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconTheme/phosphorserviceicontheme_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICEICONTHEME_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceIconTheme
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceIconTheme PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceIconTheme
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

set_target_properties(PhosphorServiceIconTheme PROPERTIES
    VERSION ${PHOSPHORSERVICEICONTHEME_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

install(TARGETS PhosphorServiceIconTheme
    EXPORT PhosphorServiceIconThemeTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorserviceicontheme_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceIconTheme
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconTheme/phosphorserviceicontheme_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceIconTheme
)

install(EXPORT PhosphorServiceIconThemeTargets
    FILE PhosphorServiceIconThemeTargets.cmake
    NAMESPACE PhosphorServiceIconTheme::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIconTheme
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceIconThemeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconThemeConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIconTheme
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconThemeConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICEICONTHEME_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconThemeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceIconThemeConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceIconTheme
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceIconTheme" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

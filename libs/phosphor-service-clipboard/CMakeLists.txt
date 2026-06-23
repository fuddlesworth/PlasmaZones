# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorServiceClipboard: a clipboard-history service for Phosphor-based
# desktop shells. Phase 2.8 of the service-library split (see
# docs/phosphor-shell-design/04-implementation-plan.md). It watches the
# session's clipboard through phosphor-wayland's data-control client, keeps a
# de-duplicated, capped, on-disk history, and can re-apply any entry. It is the
# policy / history / persistence layer over the raw ClipboardDevice primitive;
# phosphor-wayland is wrapped privately and does not appear in the public link
# interface.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSERVICECLIPBOARD_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorServiceClipboard VERSION ${PHOSPHORSERVICECLIPBOARD_VERSION} LANGUAGES CXX)
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
# PRIVATE because the data-control client (ClipboardDevice) is used inside the
# .cpp and never appears in the public headers, so consumers neither include nor
# link phosphor-wayland.
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

set(phosphorserviceclipboard_public_HDRS
    include/PhosphorServiceClipboard/PhosphorServiceClipboard.h
    include/PhosphorServiceClipboard/QmlRegistration.h
    include/PhosphorServiceClipboard/ClipboardService.h
)

set(phosphorserviceclipboard_SRCS
    src/qmlregistration.cpp
    src/clipboardservice.cpp
    src/iclipboardsource.cpp
    src/clipboardhistorymodel.cpp
    src/clipboardstore.cpp
    src/waylandclipboardsource.cpp
)

add_library(PhosphorServiceClipboard SHARED
    ${phosphorserviceclipboard_public_HDRS}
    ${phosphorserviceclipboard_SRCS}
)

add_library(PhosphorServiceClipboard::PhosphorServiceClipboard ALIAS PhosphorServiceClipboard)

generate_export_header(PhosphorServiceClipboard
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboard/phosphorserviceclipboard_export.h
    EXPORT_MACRO_NAME PHOSPHORSERVICECLIPBOARD_EXPORT
)

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

target_include_directories(PhosphorServiceClipboard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorServiceClipboard PUBLIC cxx_std_20)

target_link_libraries(PhosphorServiceClipboard
    PUBLIC
        Qt6::Core
        Qt6::Qml
    PRIVATE
        PhosphorWayland::PhosphorWayland
)

set_target_properties(PhosphorServiceClipboard PROPERTIES
    VERSION ${PHOSPHORSERVICECLIPBOARD_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorServiceClipboard
    EXPORT PhosphorServiceClipboardTargets
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES ${phosphorserviceclipboard_public_HDRS}
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceClipboard
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboard/phosphorserviceclipboard_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorServiceClipboard
)

install(EXPORT PhosphorServiceClipboardTargets
    FILE PhosphorServiceClipboardTargets.cmake
    NAMESPACE PhosphorServiceClipboard::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceClipboard
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorServiceClipboardConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboardConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceClipboard
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboardConfigVersion.cmake"
    VERSION ${PHOSPHORSERVICECLIPBOARD_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboardConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorServiceClipboardConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorServiceClipboard
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorServiceClipboard" OR BUILD_TESTING)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    endif()
endif()

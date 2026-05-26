# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSettingsUi — reusable Qt6/QML/Kirigami settings-app framework
#
# Provides the chrome (window, sidebar, breadcrumbs, footer, page host)
# and orchestration (PageController base, PageRegistry, IStagingDomain,
# ApplicationController) that any Qt6/Kirigami settings app can build on,
# so each consumer only writes its own page controllers + QML pages.
#
# Boundary:
#   - phosphor-config owns the IBackend/IGroup configuration store. This
#     library does NOT duplicate that contract. Consumers wire their own
#     ISettings implementation against phosphor-config.
#   - phosphor-shortcuts owns global-shortcut backends. Not included here.
#
# Built standalone or as a subdirectory of PlasmaZones (or any consumer).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSETTINGSUI_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSettingsUi VERSION ${PHOSPHORSETTINGSUI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorsettingsui_public_HDRS
    include/PhosphorSettingsUi/PhosphorSettingsUi.h
)

set(phosphorsettingsui_SRCS
    src/phosphorsettingsui.cpp
)

add_library(PhosphorSettingsUi SHARED
    ${phosphorsettingsui_public_HDRS}
    ${phosphorsettingsui_SRCS}
)

add_library(PhosphorSettingsUi::PhosphorSettingsUi ALIAS PhosphorSettingsUi)

generate_export_header(PhosphorSettingsUi
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorsettingsui_export.h
    EXPORT_MACRO_NAME PHOSPHORSETTINGSUI_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR locally so INSTALL_INTERFACE can reference
# it. Mirrors the pattern in libs/phosphor-config/CMakeLists.txt.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorSettingsUi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorSettingsUi PUBLIC cxx_std_20)

target_link_libraries(PhosphorSettingsUi
    PUBLIC
        Qt6::Core
        Qt6::Gui
)

set_target_properties(PhosphorSettingsUi PROPERTIES
    VERSION ${PHOSPHORSETTINGSUI_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    UNITY_BUILD OFF
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_LIBDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(NOT DEFINED KDE_INSTALL_BINDIR)
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS PhosphorSettingsUi
    EXPORT PhosphorSettingsUiTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorSettingsUiTargets
    FILE PhosphorSettingsUiTargets.cmake
    NAMESPACE PhosphorSettingsUi::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSettingsUiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfigVersion.cmake"
    VERSION ${PHOSPHORSETTINGSUI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSettingsUiConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSettingsUi
)

install(DIRECTORY include/PhosphorSettingsUi/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSettingsUi
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorsettingsui_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSettingsUi
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

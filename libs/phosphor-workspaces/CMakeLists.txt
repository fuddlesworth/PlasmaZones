# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorWorkspaces — Virtual desktop / workspace management.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORWORKSPACES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorWorkspaces VERSION ${PHOSPHORWORKSPACES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorWorkspaces SHARED
    include/PhosphorWorkspaces/VirtualDesktopManager.h
    include/PhosphorWorkspaces/ActivityManager.h
    src/VirtualDesktopManager.cpp
    src/ActivityManager.cpp
)
add_library(PhosphorWorkspaces::PhosphorWorkspaces ALIAS PhosphorWorkspaces)

generate_export_header(PhosphorWorkspaces
    EXPORT_FILE_NAME phosphorworkspaces_export.h
    EXPORT_MACRO_NAME PHOSPHORWORKSPACES_EXPORT
)

target_include_directories(PhosphorWorkspaces
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorWorkspaces PUBLIC cxx_std_20)

target_link_libraries(PhosphorWorkspaces
    PUBLIC
        Qt6::Core
        PhosphorEngine::PhosphorEngine
    PRIVATE
        Qt6::DBus
)

# Optional KActivities/PlasmaActivities support for ActivityManager
find_package(PlasmaActivities QUIET)
if(NOT PlasmaActivities_FOUND)
    find_package(KF6Activities QUIET NAMES KF6Activities KActivities)
    if(KF6Activities_FOUND)
        set(PlasmaActivities_FOUND TRUE)
    endif()
endif()
if(NOT PlasmaActivities_FOUND)
    find_package(KActivities QUIET)
    if(KActivities_FOUND)
        set(PlasmaActivities_FOUND TRUE)
    endif()
endif()
if(PlasmaActivities_FOUND)
    target_compile_definitions(PhosphorWorkspaces PRIVATE HAVE_KACTIVITIES)
    if(TARGET Plasma::Activities)
        target_link_libraries(PhosphorWorkspaces PRIVATE Plasma::Activities)
    elseif(TARGET KF6::Activities)
        target_link_libraries(PhosphorWorkspaces PRIVATE KF6::Activities)
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

set_target_properties(PhosphorWorkspaces PROPERTIES
    VERSION ${PHOSPHORWORKSPACES_VERSION}
    SOVERSION 0
)

install(TARGETS PhosphorWorkspaces
    EXPORT PhosphorWorkspacesTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorWorkspaces
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorworkspaces_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorWorkspaces
)

install(EXPORT PhosphorWorkspacesTargets
    FILE PhosphorWorkspacesTargets.cmake
    NAMESPACE PhosphorWorkspaces::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWorkspaces
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorWorkspacesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWorkspacesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWorkspaces
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWorkspacesConfigVersion.cmake"
    VERSION ${PHOSPHORWORKSPACES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWorkspacesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorWorkspacesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorWorkspaces
)

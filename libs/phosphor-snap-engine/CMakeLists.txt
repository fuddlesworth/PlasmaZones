# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorSnapEngine — Zone-based snap placement engine.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSNAPENGINE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorSnapEngine VERSION ${PHOSPHORSNAPENGINE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorSnapEngine SHARED
    include/PhosphorSnapEngine/SnapEngine.h
    include/PhosphorSnapEngine/SnapState.h
    include/PhosphorSnapEngine/ISnapSettings.h
    include/PhosphorSnapEngine/snapnavigationtargets.h
    src/SnapEngine.cpp
    src/SnapState.cpp
    src/calculate.cpp
    src/commit.cpp
    src/float.cpp
    src/lifecycle.cpp
    src/navigation.cpp
    src/navigation_actions.cpp
    src/resnap_calc.cpp
    src/snapnavigationtargets.cpp
    src/snapenginelogging.h
    src/snapenginelogging.cpp
)
add_library(PhosphorSnapEngine::PhosphorSnapEngine ALIAS PhosphorSnapEngine)

generate_export_header(PhosphorSnapEngine
    EXPORT_FILE_NAME phosphorsnapengine_export.h
    EXPORT_MACRO_NAME PHOSPHORSNAPENGINE_EXPORT
)

target_include_directories(PhosphorSnapEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorSnapEngine PUBLIC cxx_std_20)

target_link_libraries(PhosphorSnapEngine
    PUBLIC
        Qt6::Core
        Qt6::Gui
        PhosphorEngineApi::PhosphorEngineApi
        PhosphorEngineTypes::PhosphorEngineTypes
        PhosphorZones::PhosphorZones
        PhosphorGeometry::PhosphorGeometry
        PhosphorLayoutApi::PhosphorLayoutApi
        PhosphorProtocol::PhosphorProtocol
    PRIVATE
        PhosphorScreens::PhosphorScreens
        PhosphorIdentity::PhosphorIdentity
        PhosphorTiles::PhosphorTiles
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS PhosphorSnapEngine
    EXPORT PhosphorSnapEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
)

install(DIRECTORY include/PhosphorSnapEngine
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorsnapengine_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorSnapEngine
)

install(EXPORT PhosphorSnapEngineTargets
    FILE PhosphorSnapEngineTargets.cmake
    NAMESPACE PhosphorSnapEngine::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorSnapEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfigVersion.cmake"
    VERSION ${PHOSPHORSNAPENGINE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorSnapEngineConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorSnapEngine
)

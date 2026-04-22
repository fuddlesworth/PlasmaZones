# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorEngineApi — Unified placement engine contracts.
#
# Header-only INTERFACE library defining IPlacementEngine, IPlacementState,
# and NavigationContext.  Both phosphor-zones (snap) and phosphor-tiles
# (autotile) implement these interfaces so daemons and compositor plugins
# dispatch all window lifecycle and navigation through a single polymorphic
# call — zero mode branches.
#
# Depends on Qt6::Core (QString, QStringList, QRect, QJsonObject).

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORENGINEAPI_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorEngineApi VERSION ${PHOSPHORENGINEAPI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorEngineApi Library (INTERFACE — header-only)
# ═══════════════════════════════════════════════════════════════════════════════

add_library(PhosphorEngineApi INTERFACE)
add_library(PhosphorEngineApi::PhosphorEngineApi ALIAS PhosphorEngineApi)

target_include_directories(PhosphorEngineApi
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorEngineApi INTERFACE cxx_std_20)

target_link_libraries(PhosphorEngineApi
    INTERFACE
        Qt6::Core
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

install(TARGETS PhosphorEngineApi
    EXPORT PhosphorEngineApiTargets
)

install(DIRECTORY include/PhosphorEngineApi
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(EXPORT PhosphorEngineApiTargets
    FILE PhosphorEngineApiTargets.cmake
    NAMESPACE PhosphorEngineApi::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorEngineApiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfigVersion.cmake"
    VERSION ${PHOSPHORENGINEAPI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineApiConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineApi
)

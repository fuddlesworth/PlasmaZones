# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorEngineTypes — Shared engine value types and state contracts.
#
# Header-only INTERFACE library providing IPlacementState, NavigationContext,
# and shared POD types. Both domain libraries (phosphor-zones, phosphor-tiles)
# and engine libraries depend on this without pulling in the full engine API.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORENGINETYPES_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorEngineTypes VERSION ${PHOSPHORENGINETYPES_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(PhosphorEngineTypes INTERFACE)
add_library(PhosphorEngineTypes::PhosphorEngineTypes ALIAS PhosphorEngineTypes)

target_include_directories(PhosphorEngineTypes
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

target_compile_features(PhosphorEngineTypes INTERFACE cxx_std_20)

target_link_libraries(PhosphorEngineTypes
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

install(TARGETS PhosphorEngineTypes
    EXPORT PhosphorEngineTypesTargets
)

install(DIRECTORY include/PhosphorEngineTypes
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(EXPORT PhosphorEngineTypesTargets
    FILE PhosphorEngineTypesTargets.cmake
    NAMESPACE PhosphorEngineTypes::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineTypes
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorEngineTypesConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineTypesConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineTypes
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineTypesConfigVersion.cmake"
    VERSION ${PHOSPHORENGINETYPES_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineTypesConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorEngineTypesConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorEngineTypes
)

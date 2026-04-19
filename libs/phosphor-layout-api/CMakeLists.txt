# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorLayoutApi — shared layout-preview contract.
#
# A tiny library defining LayoutPreview (POD) and ILayoutSource (abstract
# interface). Both phosphor-zones (manual layouts) and phosphor-tile-algo
# (autotile algorithms — extracted later) implement ILayoutSource so editor /
# settings code can render previews without branching on isAutotile and
# without depending on either provider library directly.
#
# Header-light footprint deliberate: the contract should stay small and
# stable. Implementations carry their own dependencies (Qt + their own
# domain types) — this library carries only the shared shape.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORLAYOUTAPI_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorLayoutApi VERSION ${PHOSPHORLAYOUTAPI_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # ILayoutSource + CompositeLayoutSource now carry Q_OBJECT so AUTOMOC
    # must run standalone too. In-tree builds inherit the top-level
    # project's CMAKE_AUTOMOC setting.
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.6 REQUIRED COMPONENTS Core)

# ═══════════════════════════════════════════════════════════════════════════════
# Library
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorlayoutapi_public_HDRS
    include/PhosphorLayoutApi/PhosphorLayoutApi.h
    include/PhosphorLayoutApi/AlgorithmMetadata.h
    include/PhosphorLayoutApi/AspectRatioClass.h
    include/PhosphorLayoutApi/CompositeLayoutSource.h
    include/PhosphorLayoutApi/EdgeGaps.h
    include/PhosphorLayoutApi/GapKeys.h
    include/PhosphorLayoutApi/LayoutId.h
    include/PhosphorLayoutApi/LayoutPreview.h
    include/PhosphorLayoutApi/ILayoutSource.h
    include/PhosphorLayoutApi/ILayoutSourceRegistry.h
)

set(phosphorlayoutapi_SRCS
    src/compositelayoutsource.cpp
    src/ilayoutsource.cpp
    src/ilayoutsourceregistry.cpp
)

add_library(PhosphorLayoutApi SHARED
    ${phosphorlayoutapi_public_HDRS}
    ${phosphorlayoutapi_SRCS}
)

add_library(PhosphorLayoutApi::PhosphorLayoutApi ALIAS PhosphorLayoutApi)

generate_export_header(PhosphorLayoutApi
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorlayoutapi_export.h
    EXPORT_MACRO_NAME PHOSPHORLAYOUTAPI_EXPORT
)

# Resolve KDE_INSTALL_INCLUDEDIR here so INSTALL_INTERFACE can reference it.
# Same rationale as the other phosphor-* libs — see phosphor-config/CMakeLists
# for the full explanation.
if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
endif()

target_include_directories(PhosphorLayoutApi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
)

# C++20 as PUBLIC so consumers inherit the standard regardless of whether
# this library is the top-level project or built in-tree (matching the
# pattern used by every other phosphor-* library).
target_compile_features(PhosphorLayoutApi PUBLIC cxx_std_20)

target_link_libraries(PhosphorLayoutApi
    PUBLIC
        Qt6::Core
)

set_target_properties(PhosphorLayoutApi PROPERTIES
    VERSION ${PHOSPHORLAYOUTAPI_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Unity builds merge multiple source files into one TU, which surfaces
    # spurious "defined but not used" warnings on anonymous-namespace
    # helpers that GCC can't fully track across the merge. The translation
    # units are small enough that unity buys us nothing here.
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

install(TARGETS PhosphorLayoutApi
    EXPORT PhosphorLayoutApiTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorLayoutApiTargets
    FILE PhosphorLayoutApiTargets.cmake
    NAMESPACE PhosphorLayoutApi::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayoutApi
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorLayoutApiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayoutApiConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayoutApi
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayoutApiConfigVersion.cmake"
    VERSION ${PHOSPHORLAYOUTAPI_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayoutApiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorLayoutApiConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorLayoutApi
)

install(DIRECTORY include/PhosphorLayoutApi/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayoutApi
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/phosphorlayoutapi_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorLayoutApi
)

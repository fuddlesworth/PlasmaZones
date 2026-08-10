# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorScrollEngine — niri-style scrolling placement engine.
#
# Provides the pure ScrollStrip model (columns on an unbounded horizontal
# strip, unit-testable in isolation) and the ScrollEngine that drives it
# against the IPlacementEngine contract from phosphor-engine.

cmake_minimum_required(VERSION 3.16)

set(PHOSPHORSCROLLENGINE_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorScrollEngine VERSION ${PHOSPHORSCROLLENGINE_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

include(GenerateExportHeader)

set(CMAKE_AUTOMOC ON)

add_library(PhosphorScrollEngine SHARED
    include/PhosphorScrollEngine/ScrollTypes.h
    include/PhosphorScrollEngine/ScrollStashTypes.h
    include/PhosphorScrollEngine/ScrollStrip.h
    include/PhosphorScrollEngine/ScrollState.h
    include/PhosphorScrollEngine/ScrollEngine.h
    include/PhosphorScrollEngine/IScrollSettings.h
    src/scrollstrip_structure.cpp
    src/scrollstrip_navigation.cpp
    src/scrollstrip_relayout.cpp
    src/scrollstrip_sizing.cpp
    src/scrollengine/engine_core.cpp
    src/scrollengine/engine_overrides.cpp
    src/scrollengine/engine_context.cpp
    src/scrollengine/enginelimits.h
    src/scrollengine/engine_apply.cpp
    src/scrollengine/drag_preview.cpp
    src/scrollengine/engine_lifecycle.cpp
    src/scrollengine/engine_handoff.cpp
    src/scrollengine/engine_float.cpp
    src/scrollengine/engine_navigation.cpp
    src/scrollengine/engine_verbs.cpp
    src/scrollengine/engine_serialize.cpp
    src/scrollengine/scrollenginelogging.h
    src/scrollengine/scrollverbresolve_p.h
    src/scrollengine/scrollenginelogging.cpp
)
add_library(PhosphorScrollEngine::PhosphorScrollEngine ALIAS PhosphorScrollEngine)

generate_export_header(PhosphorScrollEngine
    EXPORT_FILE_NAME phosphorscrollengine_export.h
    EXPORT_MACRO_NAME PHOSPHORSCROLLENGINE_EXPORT
)

target_include_directories(PhosphorScrollEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_features(PhosphorScrollEngine PUBLIC cxx_std_20)

target_link_libraries(PhosphorScrollEngine
    PUBLIC
        Qt6::Core
        PhosphorEngine::PhosphorEngine
    PRIVATE
        PhosphorScreens::Runtime  # ScrollEngine queries screen geometry via ScreenManager
        PhosphorIdentity::PhosphorIdentity
)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

set_target_properties(PhosphorScrollEngine PROPERTIES
    VERSION ${PHOSPHORSCROLLENGINE_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS PhosphorScrollEngine
    EXPORT PhosphorScrollEngineTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(DIRECTORY include/PhosphorScrollEngine
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/phosphorscrollengine_export.h"
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScrollEngine
)

install(EXPORT PhosphorScrollEngineTargets
    FILE PhosphorScrollEngineTargets.cmake
    NAMESPACE PhosphorScrollEngine::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScrollEngine
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorScrollEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScrollEngineConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScrollEngine
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScrollEngineConfigVersion.cmake"
    VERSION ${PHOSPHORSCROLLENGINE_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScrollEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScrollEngineConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScrollEngine
)

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorScrollEngine" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# PhosphorScreens — domain-free screen-topology library for Qt6 Wayland.
#
# Two targets, layered so the screen-topology core does not drag QtDBus
# into pure-compute consumers:
#
#   PhosphorScreens::Core  — physical-screen monitoring, virtual-screen
#                            subdivision, identity helpers, pluggable
#                            config/panel backends. Qt6::Core/Gui only.
#                            Domain libraries (zones, snap/tile engines,
#                            placement) link this.
#   PhosphorScreens::PhosphorScreens
#                          — adds the D-Bus surface: DBusScreenAdaptor,
#                            the Plasma panel source, and the resolver.
#                            Depends on ::Core + Qt6::DBus.

cmake_minimum_required(VERSION 3.16)

# PhosphorScreens's own version — used for SOVERSION. When split to a
# standalone repo, this becomes the project() version.
set(PHOSPHORSCREENS_VERSION "0.1.0")

if(NOT PROJECT_VERSION)
    project(PhosphorScreens VERSION ${PHOSPHORSCREENS_VERSION} LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
endif()

include(GenerateExportHeader)

# ═══════════════════════════════════════════════════════════════════════════════
# Dependencies
# ═══════════════════════════════════════════════════════════════════════════════

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui DBus)

# PhosphorIdentity ships VirtualScreenId — the cross-process ID format
# helpers that VirtualScreen.h consumes. INTERFACE-only dependency, so
# zero runtime cost and no .so to chase.
if(NOT TARGET PhosphorIdentity::PhosphorIdentity)
    find_package(PhosphorIdentity CONFIG REQUIRED)
endif()

# PhosphorWayland ships the wlr-layer-shell QPA integration. ScreenManager
# consumes it for the per-screen geometry sensor windows.
if(NOT TARGET PhosphorWayland::PhosphorWayland)
    find_package(PhosphorWayland CONFIG REQUIRED)
endif()

# PhosphorProtocol::Types ships the QtCore-only org.plasmazones.* service /
# object / interface constants. Resolver.h pulls them in for the default
# endpoint — constants only, no QtDBus from this dependency.
if(NOT TARGET PhosphorProtocol::Types)
    find_package(PhosphorProtocol CONFIG REQUIRED)
endif()

if(NOT DEFINED KDE_INSTALL_INCLUDEDIR)
    include(GNUInstallDirs)
    set(KDE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(KDE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
    set(KDE_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()

# Properties shared by both library targets.
function(_phosphorscreens_target_defaults _target)
    set_target_properties(${_target} PROPERTIES
        VERSION ${PHOSPHORSCREENS_VERSION}
        SOVERSION 0
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        UNITY_BUILD OFF
        AUTOMOC ON
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_features(${_target} PUBLIC cxx_std_20)
endfunction()

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorScreens::Core — QtDBus-free screen-topology core
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscreenscore_public_HDRS
    include/PhosphorScreens/IConfigStore.h
    include/PhosphorScreens/InMemoryConfigStore.h
    include/PhosphorScreens/IPanelSource.h
    include/PhosphorScreens/IScreenProvider.h
    include/PhosphorScreens/Manager.h
    include/PhosphorScreens/NoOpPanelSource.h
    include/PhosphorScreens/PhysicalScreen.h
    include/PhosphorScreens/QtScreenProvider.h
    include/PhosphorScreens/ScreenIdentity.h
    include/PhosphorScreens/Swapper.h
    include/PhosphorScreens/VirtualScreen.h
)

add_library(PhosphorScreensCore SHARED
    ${phosphorscreenscore_public_HDRS}
    src/screenslogging.h
    src/manager.cpp
    src/manager_virtualscreens.cpp
    src/qtscreenprovider.cpp
    src/screenidentity.cpp
    src/screenslogging.cpp
    src/swapper.cpp
)

add_library(PhosphorScreens::Core ALIAS PhosphorScreensCore)

generate_export_header(PhosphorScreensCore
    BASE_NAME PhosphorScreensCore
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreenscore_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENSCORE_EXPORT
)

target_include_directories(PhosphorScreensCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorScreensCore
    PUBLIC
        Qt6::Core
        Qt6::Gui   # QRect / QRectF / QScreen in public headers
        PhosphorIdentity::PhosphorIdentity
    PRIVATE
        PhosphorWayland::PhosphorWayland  # LayerSurface for geometry sensors
)

_phosphorscreens_target_defaults(PhosphorScreensCore)

# ═══════════════════════════════════════════════════════════════════════════════
# PhosphorScreens — D-Bus adaptor + Plasma panel source + resolver
# ═══════════════════════════════════════════════════════════════════════════════

set(phosphorscreens_public_HDRS
    include/PhosphorScreens/DBusScreenAdaptor.h
    include/PhosphorScreens/PhosphorScreens.h
    include/PhosphorScreens/PlasmaPanelSource.h
    include/PhosphorScreens/Resolver.h
)

add_library(PhosphorScreens SHARED
    ${phosphorscreens_public_HDRS}
    src/dbusscreenadaptor.cpp
    src/plasmapanelsource.cpp
    src/resolver.cpp
)

add_library(PhosphorScreens::PhosphorScreens ALIAS PhosphorScreens)

generate_export_header(PhosphorScreens
    BASE_NAME PhosphorScreens
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    EXPORT_MACRO_NAME PHOSPHORSCREENS_EXPORT
)

target_include_directories(PhosphorScreens
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PhosphorScreens
    PUBLIC
        PhosphorScreens::Core
        Qt6::DBus  # QDBusAbstractAdaptor in DBusScreenAdaptor.h public surface
        PhosphorProtocol::Types  # Service::* constants in Resolver.h
)

_phosphorscreens_target_defaults(PhosphorScreens)

# ═══════════════════════════════════════════════════════════════════════════════
# Install
# ═══════════════════════════════════════════════════════════════════════════════

install(TARGETS PhosphorScreensCore PhosphorScreens
    EXPORT PhosphorScreensTargets
    LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${KDE_INSTALL_BINDIR}
)

install(EXPORT PhosphorScreensTargets
    FILE PhosphorScreensTargets.cmake
    NAMESPACE PhosphorScreens::
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhosphorScreensConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfig.cmake"
    INSTALL_DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfigVersion.cmake"
    VERSION ${PHOSPHORSCREENS_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PhosphorScreensConfigVersion.cmake"
    DESTINATION ${KDE_INSTALL_LIBDIR}/cmake/PhosphorScreens
)

install(DIRECTORY include/PhosphorScreens/
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScreens
    FILES_MATCHING PATTERN "*.h"
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreenscore_export.h
    ${CMAKE_CURRENT_BINARY_DIR}/phosphorscreens_export.h
    DESTINATION ${KDE_INSTALL_INCLUDEDIR}/PhosphorScreens
)

# ═══════════════════════════════════════════════════════════════════════════════
# Tests
# ═══════════════════════════════════════════════════════════════════════════════

if(CMAKE_PROJECT_NAME STREQUAL "PhosphorScreens" OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

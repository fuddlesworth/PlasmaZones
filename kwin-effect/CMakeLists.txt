cmake_minimum_required(VERSION 3.22...4.2)
project(PlasmaZonesEffect)

# PlasmaZones KWin Effect (C++ Plugin)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Requires KWin from Plasma 6.6+ (prePaintWindow(RenderView*,...), paintWindow(Region), no postPaintWindow).

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Widgets)
set(QT_DEFAULT_MAJOR_VERSION 6)

# Find KWin for effect development
find_package(KWin 6.6 REQUIRED)

# KWin effect plugins link against libkwin. Log the build-time version so
# packagers can verify compatibility (patch releases are ABI-compatible).
if(KWin_FOUND AND KWin_DIR)
    file(READ "${KWin_DIR}/KWinConfigVersion.cmake" _kwin_version_file)
    string(REGEX MATCH "set\\(PACKAGE_VERSION \"([0-9.]+)\"\\)" _ "${_kwin_version_file}")
    if(CMAKE_MATCH_1)
        message(STATUS "PlasmaZones KWin effect: building against KWin ${CMAKE_MATCH_1} (plugin IID locked; rebuild when KWin updates)")
    endif()
endif()

# Tell AutoMoc about KWin's effect factory macro (suppresses false positive warning)
list(APPEND CMAKE_AUTOMOC_MACRO_NAMES "KWIN_EFFECT_FACTORY_SUPPORTED")

# Effect sources
set(plasmazones_effect_SRCS
    plasmazoneseffect.cpp
    plasmazoneseffect.h
    autotilehandler.cpp
    autotilehandler.h
    autotilehandler/signals.cpp
    autotilehandler/tiling.cpp
    navigationhandler.cpp
    navigationhandler.h
    screenchangehandler.cpp
    screenchangehandler.h
    snapassisthandler.cpp
    snapassisthandler.h
    windowanimator.cpp
    windowanimator.h
    dragtracker.cpp
    dragtracker.h
    ${CMAKE_SOURCE_DIR}/shared/virtualscreenid.h
)

# Create the KWin effect plugin using KDE's plugin macro
# This installs to ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins/
kcoreaddons_add_plugin(kwin_effect_plasmazones
    SOURCES ${plasmazones_effect_SRCS}
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

# Link required libraries
target_link_libraries(kwin_effect_plasmazones PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
)

# Include directories
target_include_directories(kwin_effect_plasmazones PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}  # For shared/ headers (virtualscreenid.h)
)

# Note: metadata.json is embedded in the plugin binary via Q_PLUGIN_METADATA in
# the KWIN_EFFECT_FACTORY_SUPPORTED macro. The source file MUST include
# "plasmazoneseffect.moc" at the end for MOC to process the generated Q_OBJECT class.

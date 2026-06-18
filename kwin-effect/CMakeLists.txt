# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones KWin Effect (C++ Plugin) — requires KWin from Plasma 6.7+
# (prePaintWindow(RenderView*,...), paintWindow(Region), no postPaintWindow).
#
# Only included via add_subdirectory() from the top-level project; the
# parent provides cmake_minimum_required, project(), ECM, and Qt. Calling
# project() here would re-run compiler detection and double-include
# KDECompilerSettings.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Widgets dropped — no QWidget / QtWidgets usage in kwin-effect/.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui DBus Qml Quick Svg)
set(QT_DEFAULT_MAJOR_VERSION 6)

# Find KWin for effect development
find_package(KWin 6.7 REQUIRED)

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
    plasmazoneseffect/lifecycle.cpp
    plasmazoneseffect/borders.cpp
    plasmazoneseffect/window_identity.cpp
    plasmazoneseffect/mouse_drag.cpp
    plasmazoneseffect/window_filtering.cpp
    plasmazoneseffect/screens.cpp
    plasmazoneseffect/window_lifecycle.cpp
    plasmazoneseffect/daemon_bringup.cpp
    plasmazoneseffect/daemon_apply.cpp
    plasmazoneseffect/drag_snap.cpp
    plasmazoneseffect/paint_pipeline.cpp
    plasmazoneseffect/shader_transitions.cpp
    plasmazoneseffect/shader_internal.h
    plasmazoneseffect/types.h
    shadertransitionmanager.h
    shadertransitionmanager.cpp
    autotilehandler.cpp
    autotilehandler.h
    autotilehandler/signals.cpp
    autotilehandler/state.cpp
    autotilehandler/tiling.cpp
    navigationhandler.cpp
    navigationhandler.h
    screenchangehandler.cpp
    screenchangehandler.h
    snapassisthandler.cpp
    snapassisthandler.h
    snapassistthumbnailcapture.cpp
    snapassistthumbnailcapture.h
    windowanimator.cpp
    windowanimator.h
    compositorclock.cpp
    compositorclock.h
    dragtracker.cpp
    dragtracker.h
    # virtualscreenid moved to libs/phosphor-identity (PhosphorIdentity::VirtualScreenId).
    # Consumers include <PhosphorIdentity/VirtualScreenId.h> directly; reachable
    # transitively via PhosphorCompositor::PhosphorCompositor's PhosphorIdentity link.
    kwin_compositor_bridge.cpp
    kwin_compositor_bridge.h
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
    # Snap-assist thumbnail capture: OffscreenQuickScene loads a tiny QML
    # scene that hosts org.kde.kwin's WindowThumbnail. Both Qml (engine,
    # setProperty) and Quick (QQuickItem* root) are needed.
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
    PhosphorCompositor::PhosphorCompositor
    # `plasmazoneseffect.h` directly includes `<PhosphorEngine/EngineTypes.h>`
    # for the WindowKind enum used by classifyWindowKind. Direct include →
    # direct link, matching the PhosphorAnimation rationale below.
    PhosphorEngine::PhosphorEngine
    PhosphorProtocol::PhosphorProtocol
    # Explicit link: `compositorclock.h` and `windowanimator.h` directly
    # include `<PhosphorAnimation/...>`. The dependency is reachable
    # transitively via `PhosphorCompositor::PhosphorCompositor`, but depending
    # on a transitive target for a directly-included header is brittle
    # — a future reshuffle of compositor-common's link graph could drop
    # the re-export and this target would break mysteriously. Named
    # here so the link graph matches the include graph.
    PhosphorAnimation::PhosphorAnimation
    # ShaderIncludeResolver: kwin-effect resolves `#include` directives
    # in animation shader source so the canonical header inlines before
    # the `#define PLASMAZONES_KWIN` injection selects its default-block
    # branch (KWin::GLShader's classic-GL pipeline doesn't bind UBOs).
    PhosphorShaders::PhosphorShaders
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

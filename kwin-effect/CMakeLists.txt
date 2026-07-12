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
# Qml/Quick dropped — snap-assist capture renders windows into a GLFramebuffer
# directly (KWin 6.7 removed the OffscreenQuickScene readback that needed them).
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui DBus Svg)
set(QT_DEFAULT_MAJOR_VERSION 6)

# Find KWin for effect development
find_package(KWin 6.7 REQUIRED)

# libepoxy: snap-assist thumbnail dma-buf export (Phase-0 spike) uses
# EGL_MESA_image_dma_buf_export to hand the rendered OffscreenQuickScene
# texture to the daemon zero-copy. epoxy provides the EGL/GL entry points
# (eglExportDMABUFImageMESA et al.) the effect calls; KWin itself links it,
# so it is always present where the effect runs.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Epoxy REQUIRED IMPORTED_TARGET epoxy)

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
    plasmazoneseffect/decorations.cpp
    plasmazoneseffect/decoration_render.cpp
    plasmazoneseffect/surfacelayers.cpp
    plasmazoneseffect/surface_backdrop.cpp
    plasmazoneseffect/surface_audio.cpp
    plasmazoneseffect/window_identity.cpp
    plasmazoneseffect/mouse_drag.cpp
    plasmazoneseffect/window_filtering.cpp
    plasmazoneseffect/window_query.cpp
    plasmazoneseffect/window_query.h
    plasmazoneseffect/screens.cpp
    plasmazoneseffect/window_lifecycle.cpp
    plasmazoneseffect/daemon_bringup.cpp
    plasmazoneseffect/daemon_apply.cpp
    plasmazoneseffect/drag_snap.cpp
    plasmazoneseffect/drag_end.cpp
    plasmazoneseffect/mesh_sim.cpp
    plasmazoneseffect/paint_pipeline.cpp
    plasmazoneseffect/shader_transitions.cpp
    plasmazoneseffect/surface_compile.cpp
    plasmazoneseffect/shader_resolve.cpp
    plasmazoneseffect/shader_resolve.h
    plasmazoneseffect/shader_internal.h
    plasmazoneseffect/types.h
    shadertransitionmanager.h
    shadertransitionmanager.cpp
    autotilehandler.cpp
    autotilehandler.h
    autotilehandler/signals.cpp
    autotilehandler/state.cpp
    autotilehandler/tiling.cpp
    autotilehandler/wiring.cpp
    snaphandler.cpp
    snaphandler.h
    desktoptransitionmanager.cpp
    desktoptransitioncapture.cpp
    desktoptransitionmanager.h
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

# Keep the epoxy TU out of the Unity (jumbo) build (root sets CMAKE_UNITY_BUILD
# ON). snapassistthumbnailcapture.cpp must include epoxy/gl.h before any other
# GL header; jumbo-grouping it with the other effect sources (which pull in
# KWin's GL headers) would break that ordering. Mirrors the daemon's identical
# guard on dmabufglimport.cpp (src/CMakeLists.txt).
set_source_files_properties(snapassistthumbnailcapture.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# desktoptransitionmanager.cpp defines a file-local drawDesktopBlendQuad + a
# fullscreen vertex-source constant; keep it out of the Unity blob so those
# never ODR-collide with surfacelayers.cpp's equivalents.
set_source_files_properties(desktoptransitionmanager.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# desktoptransitioncapture.cpp holds the capture half of the same class, including
# its own file-local helpers (captureFormatFor, allocateOutputTexture); same
# reason, same exclusion.
set_source_files_properties(desktoptransitioncapture.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# Link required libraries
target_link_libraries(kwin_effect_plasmazones PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
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
    # PhosphorRules: the unified LGPL rule engine. The effect resolves
    # its per-window animation overrides AND its exclusion gates through
    # RuleEvaluator over slices of rules.json — the snap-drag gate
    # binds to `ExclusionRules::excludeRulesFrom`, the animation gate to
    # `ExclusionRules::excludeAnimationsRulesFrom`, both filtering the
    # same unified rule-store push that drives the OverrideAnimation*
    # pipeline. Both the effect and the daemon link this one library so
    # there is exactly one match-code implementation (GPL->LGPL link is
    # fine).
    PhosphorRules::PhosphorRules
    # ShaderIncludeResolver: kwin-effect resolves `#include` directives
    # in animation shader source so the canonical header inlines before
    # the `#define PLASMAZONES_KWIN` injection selects its default-block
    # branch (KWin::GLShader's classic-GL pipeline doesn't bind UBOs).
    PhosphorShaders::PhosphorShaders
    # PhosphorSurface: the surface-shader pack registry/contract. The effect
    # compiles the selected surface pack (window border / rounded corners) from
    # data/surface via SurfaceShaderRegistry, the third shader-pack category
    # alongside animations + overlay shaders. GPL->LGPL link is fine.
    PhosphorSurface::PhosphorSurface
    # PhosphorAudio: the CAVA spectrum provider (Qt-Core-only). The effect runs
    # its OWN CavaSpectrumProvider to feed audio-reactive surface decorations
    # (border-audio), since the daemon's spectrum is not exported over the
    # low-frequency D-Bus config channel. GPL->LGPL link is fine.
    PhosphorAudio::PhosphorAudio
    # libepoxy: EGL dma-buf export entry points for snap-assist thumbnail
    # zero-copy delivery (snapassistthumbnailcapture.cpp). Qt6::DBus's
    # QDBusUnixFileDescriptor carries the exported fd to the daemon.
    PkgConfig::Epoxy
)

# Include directories
target_include_directories(kwin_effect_plasmazones PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Note: metadata.json is embedded in the plugin binary via Q_PLUGIN_METADATA in
# the KWIN_EFFECT_FACTORY_SUPPORTED macro. The source file MUST include
# "plasmazoneseffect.moc" at the end for MOC to process the generated Q_OBJECT class.

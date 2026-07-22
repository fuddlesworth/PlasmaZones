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
    plasmazoneseffect/plasmazoneseffect.cpp
    plasmazoneseffect/plasmazoneseffect.h
    plasmazoneseffect/lifecycle.cpp
    plasmazoneseffect/lifecycle_wiring.cpp
    plasmazoneseffect/decoration_appearance.cpp
    plasmazoneseffect/decorations.cpp
    plasmazoneseffect/decoration_teardown.cpp
    plasmazoneseffect/decoration_render.cpp
    plasmazoneseffect/surfacelayers.cpp
    plasmazoneseffect/surface_backdrop.cpp
    plasmazoneseffect/surface_capture.cpp
    plasmazoneseffect/surface_gating.cpp
    plasmazoneseffect/decoration_rules.cpp
    plasmazoneseffect/surface_audio.cpp
    plasmazoneseffect/window_identity.cpp
    plasmazoneseffect/mouse_drag.cpp
    plasmazoneseffect/window_filtering.cpp
    plasmazoneseffect/window_query.cpp
    plasmazoneseffect/window_query.h
    plasmazoneseffect/screens.cpp
    plasmazoneseffect/window_lifecycle.cpp
    plasmazoneseffect/window_connections.cpp
    plasmazoneseffect/daemon_bringup.cpp
    plasmazoneseffect/daemon_settings.cpp
    plasmazoneseffect/daemon_apply.cpp
    plasmazoneseffect/rule_invalidation.cpp
    plasmazoneseffect/drag_snap.cpp
    plasmazoneseffect/drag_end.cpp
    plasmazoneseffect/mesh_sim.cpp
    plasmazoneseffect/mesh_sim.h
    plasmazoneseffect/paint_pipeline.cpp
    plasmazoneseffect/paint_shader_window.cpp
    plasmazoneseffect/paint_capture.cpp
    plasmazoneseffect/paint_internal.h
    plasmazoneseffect/shader_transitions.cpp
    plasmazoneseffect/shader_config_dbus.cpp
    plasmazoneseffect/shader_textures.cpp
    plasmazoneseffect/surface_compile.cpp
    plasmazoneseffect/shader_resolve.cpp
    plasmazoneseffect/shader_resolve.h
    plasmazoneseffect/shader_internal.h
    plasmazoneseffect/surface_types.h
    plasmazoneseffect/transition_types.h
    plasmazoneseffect/effect_state.h
    plasmazoneseffect/types.h
    plasmazoneseffect/surface_fold.h
    transitions/shadertransitionmanager.h
    transitions/shadertransitionmanager.cpp
    transitions/desktoptransitionmanager.cpp
    transitions/desktoptransitioncapture.cpp
    transitions/desktoptransitionshader.cpp
    transitions/desktoptransitionteardown.cpp
    transitions/desktoptransitionmanager.h
    autotilehandler/autotilehandler.cpp
    autotilehandler/autotilehandler.h
    autotilehandler/signals.cpp
    autotilehandler/state.cpp
    autotilehandler/tiling.cpp
    autotilehandler/wiring.cpp
    handlers/snaphandler.cpp
    handlers/snaphandler.h
    handlers/navigationhandler.cpp
    handlers/navigationhandler.h
    handlers/screenchangehandler.cpp
    handlers/screenchangehandler.h
    handlers/snapassisthandler.cpp
    handlers/snapassisthandler.h
    handlers/dragtracker.cpp
    handlers/dragtracker.h
    compositor/snapassistthumbnailcapture.cpp
    compositor/snapassistthumbnailcapture.h
    compositor/windowanimator.cpp
    compositor/windowanimator.h
    compositor/compositorclock.cpp
    compositor/compositorclock.h
    compositor/deferredwindowcommits.h
    # virtualscreenid moved to libs/phosphor-identity (PhosphorIdentity::VirtualScreenId).
    # Consumers include <PhosphorIdentity/VirtualScreenId.h> directly; reachable
    # transitively via PhosphorCompositor::PhosphorCompositor's PhosphorIdentity link.
    compositor/compositorbridge.cpp
    compositor/compositorbridge.h
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
set_source_files_properties(compositor/snapassistthumbnailcapture.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# All four TUs of DesktopTransitionManager stay out of the Unity blob.
#
# None of them fixes a PRESENT collision, and the comments here used to imply
# otherwise. Every file-local name across the four is unique repo-wide
# (drawDesktopBlendQuad, translatePackParams, captureFormatFor,
# allocateOutputTexture), and desktoptransitionmanager.cpp:42 is explicit that
# its quad helper was given a distinct name from surfacelayers.cpp's
# drawFullscreenQuad precisely so the blob CANNOT collide — differently-named
# anonymous-namespace entities never do. The remaining two TUs carry no
# file-local entity at all: the vertex source in desktoptransitionshader.cpp is
# a FUNCTION-local static, and desktoptransitionteardown.cpp has no anonymous
# namespace.
#
# The exclusion is a uniform prophylactic rule instead: this class is split by
# concern across four files that freely add file-local helpers, and a future one
# sharing a name with any other blob member would fail the build in a way that
# reads as unrelated. Keeping the whole class out costs only blob parallelism
# and cross-TU inlining, neither of which matters for four small TUs.
set_source_files_properties(transitions/desktoptransitionmanager.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(transitions/desktoptransitioncapture.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(transitions/desktoptransitionshader.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(transitions/desktoptransitionteardown.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

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

# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic shared library for PlasmaZones compositor plugins.
# Contains JSON parsing, trigger marshalling, and other portable code
# shared by the KWin effect, Wayfire plugin, and future compositor
# integrations.  D-Bus wire types and service constants moved to
# PhosphorProtocol.
#
# Animation primitives (EasingCurve, WindowAnimation, AnimationConfig,
# AnimationMath, applyStaggeredOrImmediate) moved to PhosphorAnimation.
# Callers include <PhosphorAnimation/…> headers directly; this library
# transitively exposes them via its PUBLIC link to PhosphorAnimation.

# NOTE: If this ever becomes a shared (SHARED) library, all public API types
# (BorderState, WindowInfo, etc.) will need PLASMAZONES_EXPORT macros.
add_library(plasmazones_compositor_common STATIC
    snap_assist_filter.cpp
    snap_assist_filter.h
    trigger_parser.cpp
    trigger_parser.h
    # screen_id.{h,cpp} moved to libs/phosphor-identity (PhosphorIdentity::ScreenId).
    # Consumers include <PhosphorIdentity/ScreenId.h> directly — no forwarding shim.
    #
    # window_id moved to libs/phosphor-identity. Consumers include
    # <PhosphorIdentity/WindowId.h> directly — no forwarding shim.
    #
    # Animation primitives moved to libs/phosphor-animation. Consumers
    # include <PhosphorAnimation/{Easing,WindowMotion,AnimationMath,
    # StaggerTimer}.h> directly — no forwarding shim.
    #
    # D-Bus wire types (dbus_types), constants (dbus_constants), helpers
    # (dbus_helpers), and logging moved to libs/phosphor-protocol.
    # Consumers include <PhosphorProtocol/…> headers directly.
    autotile_state.h
    compositor_bridge.h
    debounced_action.h
    floating_cache.h
    geometry_helpers.h
)

target_include_directories(plasmazones_compositor_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plasmazones_compositor_common PUBLIC
    Qt6::Core
    Qt6::Gui
    PhosphorProtocol::PhosphorProtocol
    PhosphorIdentity::PhosphorIdentity
    PhosphorAnimation::PhosphorAnimation
)

# C++20 required for std::chrono features
target_compile_features(plasmazones_compositor_common PUBLIC cxx_std_20)

# PIC required because this static lib is linked into shared objects (KWin effect .so)
set_target_properties(plasmazones_compositor_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

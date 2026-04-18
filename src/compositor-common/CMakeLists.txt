# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic shared library for PlasmaZones compositor plugins.
# Contains D-Bus helpers, JSON parsing, trigger marshalling, and other
# portable code shared by the KWin effect, Wayfire plugin, and future
# compositor integrations.
#
# Animation primitives (EasingCurve, WindowAnimation, AnimationConfig,
# AnimationMath, applyStaggeredOrImmediate) moved to PhosphorAnimation.
# Callers include <PhosphorAnimation/…> headers directly; this library
# transitively exposes them via its PUBLIC link to PhosphorAnimation.

# NOTE: If this ever becomes a shared (SHARED) library, all public API types
# (BorderState, WindowInfo, etc.) will need PLASMAZONES_EXPORT macros.
add_library(plasmazones_compositor_common STATIC
    logging.cpp
    logging.h
    snap_assist_filter.cpp
    snap_assist_filter.h
    trigger_parser.cpp
    trigger_parser.h
    screen_id.cpp
    screen_id.h
    # window_id moved to libs/phosphor-identity — backward-compat shim
    # `window_id.h` in this directory forwards to <PhosphorIdentity/WindowId.h>
    # so the KWin effect's existing `#include "window_id.h"` keeps working.
    # The shim is header-only; it's reachable via the public include-dir
    # wired below and does not need to be listed as a source.
    #
    # Animation primitives moved to libs/phosphor-animation.
    # #include <PhosphorAnimation/{Easing,WindowMotion,AnimationMath,
    # StaggerTimer}.h> directly — no forwarding shim layer.
    autotile_state.h
    compositor_bridge.h
    dbus_types.cpp
    dbus_constants.h
    debounced_action.h
    dbus_helpers.h
    dbus_types.h
    floating_cache.h
    geometry_helpers.h
)

target_include_directories(plasmazones_compositor_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plasmazones_compositor_common PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    PhosphorIdentity::PhosphorIdentity
    PhosphorAnimation::PhosphorAnimation
)

# C++20 required for std::chrono features
target_compile_features(plasmazones_compositor_common PUBLIC cxx_std_20)

# PIC required because this static lib is linked into shared objects (KWin effect .so)
set_target_properties(plasmazones_compositor_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

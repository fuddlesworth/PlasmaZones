# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compositor-agnostic shared library for PlasmaZones compositor plugins.
# Contains easing curves, animation math, D-Bus helpers, JSON parsing,
# trigger parsing, and other portable code shared by KWin effect,
# Wayfire plugin, and future compositor integrations.

# NOTE: If this ever becomes a shared (SHARED) library, all public API types
# (BorderState, WindowInfo, EasingCurve, etc.) will need PLASMAZONES_EXPORT macros.
add_library(plasmazones_compositor_common STATIC
    logging.cpp
    logging.h
    easingcurve.cpp
    easingcurve.h
    animation_math.cpp
    animation_math.h
    snap_assist_filter.cpp
    snap_assist_filter.h
    stagger_timer.cpp
    stagger_timer.h
    trigger_parser.cpp
    trigger_parser.h
    # screen_id.{h,cpp} moved to libs/phosphor-identity (PhosphorIdentity::ScreenId).
    # Consumers include <PhosphorIdentity/ScreenId.h> directly.
    # window_id moved to libs/phosphor-identity — backward-compat shim
    # `window_id.h` in this directory forwards to <PhosphorIdentity/WindowId.h>
    # so the KWin effect's existing `#include "window_id.h"` keeps working.
    # The shim is header-only; it's reachable via the public include-dir
    # wired below and does not need to be listed as a source.
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
)

# C++20 required for std::chrono features
target_compile_features(plasmazones_compositor_common PUBLIC cxx_std_20)

# PIC required because this static lib is linked into shared objects (KWin effect .so)
set_target_properties(plasmazones_compositor_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file AnimationLimits.h
 * @brief Animation-wide UI bounds (duration, stagger interval).
 *
 * Source-of-truth min/max values surfaced by the settings UI's
 * duration and stagger-interval sliders. They are animation-wide
 * policy, so they MUST live in the animation library and not in any
 * consumer-specific constants header. Previously homed under
 * `PhosphorTiles::AutotileDefaults` for historical reasons; that
 * placement was a layering mistake (the autotile library has no
 * authority over generic animation policy) and has been corrected.
 *
 * The duration bounds are ENFORCED clamps, not merely slider policy,
 * on the compositor paths that ARM an animation lifetime: the
 * settings load (`daemon_bringup.cpp`), the per-window shader
 * transition (`shader_transitions.cpp`) and the desktop switch
 * (`desktoptransitionmanager.cpp`). There they bound how long a
 * transition may hold per-frame repaints — and, for the desktop
 * switch, the fullscreen-effect claim — so a hand-edited per-event
 * profile JSON cannot arm a multi-minute animation. The Rule
 * timing slot clamps its own candidate against them too
 * (`shader_resolve.cpp`). Changing a bound changes runtime behaviour
 * on those paths, not just the slider range.
 *
 * NOT universal: the daemon's `SurfaceAnimator` (OSD / popup / overlay
 * surfaces) reads `Profile::effectiveDuration()` raw and is bounded
 * only by `Profile::MaxDurationMs`, and the autotile library does not
 * reference these constants at all. Do not assume a duration reaching
 * you has been clamped — clamp at the site that arms the lifetime.
 */
namespace PhosphorAnimation {
namespace Limits {

/// Minimum animation duration in milliseconds. Below ~50 ms the
/// transition is so brief the user perceives it as a snap.
constexpr int MinAnimationDurationMs = 50;

/// Maximum animation duration in milliseconds. Two seconds covers
/// every reasonable use case (deliberate, drawn-out OSD reveals;
/// slow-motion debugging) without letting a misconfigured tree
/// freeze a popup for an unreasonable interval.
constexpr int MaxAnimationDurationMs = 2000;

/// Default animation duration in milliseconds, used as the fallback
/// at startup before settings are loaded from the daemon. 150 ms is
/// short enough to feel snappy on legitimate rapid window churn but
/// long enough that a user with a fresh install sees the transition
/// rather than what looks like a hard cut. MUST sit within
/// [MinAnimationDurationMs, MaxAnimationDurationMs] so an unconditional
/// init through this constant is structurally safe — settings reload
/// further refines it.
constexpr int DefaultAnimationDurationMs = 150;
static_assert(DefaultAnimationDurationMs >= MinAnimationDurationMs
                  && DefaultAnimationDurationMs <= MaxAnimationDurationMs,
              "DefaultAnimationDurationMs must lie within [Min, Max] so callers get a structurally-safe init");

/// Minimum stagger interval between sequenced animations in
/// milliseconds. Below 10 ms the staggering blurs into a single
/// simultaneous burst.
constexpr int MinAnimationStaggerIntervalMs = 10;

/// Maximum stagger interval between sequenced animations in
/// milliseconds. 200 ms × N items is enough to feel deliberate
/// without making large lists glacial.
constexpr int MaxAnimationStaggerIntervalMs = 200;

/// Hard ceiling on the per-frame `iTimeDelta` pushed into shaders, in
/// seconds. Both runtimes (the daemon's overlay shader update path and
/// the surface-animator's compositor-side push) clamp the steady-clock
/// frame delta against this so a sleep/resume hiccup, GC stall, or
/// scheduler glitch does not blast a multi-second jump into shaders
/// that integrate `iTimeDelta` (sparkle drift, particle motion, noise
/// advance). 100 ms is generous: at the worst-cap a single tick
/// represents 6 frames worth of motion at 60 Hz, beyond which the
/// effect "skips" rather than blurring through unrealistic motion.
/// Pinned in this header so a future bump propagates to BOTH runtimes
/// without one falling out of sync.
constexpr float MaxShaderTimeDeltaSeconds = 0.1f;

} // namespace Limits
} // namespace PhosphorAnimation

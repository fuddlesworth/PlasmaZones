// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file AnimationLimits.h
 * @brief Animation-wide UI bounds (duration, stagger interval).
 *
 * Source-of-truth min/max values surfaced by the settings UI's
 * duration and stagger-interval sliders. Every animation in the system
 * — global default, per-event override, autotile window-move,
 * overlay/OSD/popup transitions — clamps against these limits, so they
 * MUST live in the animation library and not in any consumer-specific
 * constants header. Previously homed under
 * `PhosphorTiles::AutotileDefaults` for historical reasons; that
 * placement was a layering mistake (the autotile library has no
 * authority over generic animation policy) and has been corrected.
 *
 * The values are pure UI policy — no algorithm or runtime path
 * enforces them as clamps. Increasing the upper bound has no effect
 * beyond letting the user pick larger values in the slider.
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

} // namespace Limits
} // namespace PhosphorAnimation

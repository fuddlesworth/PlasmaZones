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
 * The duration bounds are ENFORCED clamps, not merely slider policy.
 * They cap how long an animation may hold per-frame repaints — and, for
 * the desktop switch, the fullscreen-effect claim — so a hand-edited
 * per-event profile JSON cannot arm a multi-minute animation. Five call
 * sites clamp against them (keep in sync when adding one):
 *  - the settings load (`daemon_bringup.cpp`),
 *  - the Rule timing slot, TWICE — once in
 *    `resolveAnimationShaderAndDuration` and once in
 *    `resolveAnimationMotionProfile` (both `shader_resolve.cpp`),
 *  - `PlasmaZonesEffect::resolveEventMotionProfile`
 *    (`shader_transitions.cpp`), which bounds the motion cascade's
 *    resolved DURATION at the source,
 *  - `ShaderInternal::resolveTransitionLifetimeMs`
 *    (`kwin-effect/plasmazoneseffect/shader_internal.h`), which bounds a
 *    shader transition's LIFETIME and folds in the spring settle-time
 *    rule. Both the per-window transition (`shader_transitions.cpp`) and
 *    the desktop switch (`desktoptransitionmanager.cpp`) route through
 *    it.
 * Changing a bound changes runtime behaviour on those paths, not just
 * the slider range.
 *
 * These bounds constrain a DURATION. They do NOT bound a STATEFUL
 * (spring) curve, which derives its lifetime from its own physics and
 * ignores the duration entirely — see `AnimatedValue::advance`. Absent
 * anything else a spring is bounded only by `Curve::settleTime()`, itself
 * capped at 30 s inside `Spring`. That is a PHYSICS bound, and it is the
 * right one for a consumer deliberately outside this envelope (the
 * daemon's `SurfaceAnimator`).
 *
 * It is NOT enough for the compositor, whose two legs would otherwise
 * disagree about the same curve: the shader leg cuts at
 * `MaxAnimationDurationMs` via `ShaderInternal::resolveTransitionLifetimeMs`,
 * while the geometry leg would run out to the raw settle time — and a
 * SLIDER-reachable soft spring (`zeta*omega < 2.649`, e.g. "spring:10,0.15")
 * settles in 3.5 s, leaving the window animating for seconds after its
 * shader was torn down. So `WindowAnimator` resolves the lifetime through
 * that same helper and passes it as `MotionSpec::maxLifetimeMs`, and
 * `AnimatedValue::advance` folds it into the completion test. The geometry leg
 * can therefore never OUTLIVE the shader leg — which is the failure that
 * mattered. They are not identical at the fast end: a spring settling under
 * `MinAnimationDurationMs` completes the geometry at its true settle time while
 * the shader is floored to 50 ms and simply holds at iTime ≈ 1, which is
 * harmless.
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

/// Hard ceiling on a per-frame time delta handed to a shader or to a
/// physics integrator, in seconds. A sleep/resume hiccup, GC stall, or
/// scheduler glitch must not blast a multi-second jump into a consumer
/// that integrates the delta — shaders that advance state from
/// `iTimeDelta` (sparkle drift, particle motion, noise advance), and
/// spring curves, whose `step()` is semi-implicit Euler and is only
/// stable for `dt < 1/(5*omega)`. 100 ms is generous: at the cap a
/// single tick is 6 frames of motion at 60 Hz, beyond which the effect
/// "skips" rather than blurring through unrealistic motion.
///
/// Every WALL-CLOCK delta producer clamps against this (keep in sync
/// when adding one):
///  - `AnimatedValue::advance` (`AnimatedValue.h`) — this library's own
///    integrator, and the one every spring-curve animation runs on,
///  - the daemon's overlay shader push (`overlayservice/shader.cpp`),
///  - the daemon-side `SurfaceAnimator`'s shader delta
///    (`surfaceanimator.cpp`),
///  - the compositor's `iTimeDelta` uniform
///    (`plasmazoneseffect/paint_pipeline.cpp`),
///  - `ShaderInternal::easeProgress`
///    (`plasmazoneseffect/shader_internal.h`), the SINGLE clamp for the
///    spring integrator's dt on BOTH compositor paint paths — the
///    per-window transition and the desktop switch, which share it.
///    Neither `paint_pipeline.cpp` nor `desktoptransitionmanager.cpp`
///    clamps the integrator dt itself; they route through this helper.
///    The desktop switch uploads no `iTimeDelta` uniform.
/// A synthetic fixed step (`tools/shader-render` renders at 1/fps)
/// cannot spike and needs no clamp.
///
/// NOT clamped, and deliberately out of this library's reach:
/// `PhosphorRendering::ShaderEffect::onPlayingTick`
/// (`libs/phosphor-rendering/src/shadereffect.cpp`) pushes a raw
/// wall-clock delta into a QML-hosted shader. phosphor-rendering does
/// not link phosphor-animation, so wiring it up means adding that
/// dependency — a layering call, not a drive-by. It self-mitigates a
/// re-show (it rebases the clock while hidden) but not a stall while
/// visible.
constexpr float MaxShaderTimeDeltaSeconds = 0.1f;

} // namespace Limits
} // namespace PhosphorAnimation

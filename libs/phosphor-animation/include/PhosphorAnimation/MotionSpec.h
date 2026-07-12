// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <functional>
#include <optional>

namespace PhosphorAnimation {

class IMotionClock;

/// Runtime call-site bundle for starting an AnimatedValue<T>.
///
/// Profile is the serializable config surface; MotionSpec adds per-animation
/// runtime concerns: clock, retarget policy, callbacks. Clock is non-owning
/// (outlives all AnimatedValues). Callbacks MUST NOT throw — the library does
/// not catch exceptions out of them.
template<typename T>
struct MotionSpec
{
    Profile profile;

    /// Non-owning; non-null required.
    IMotionClock* clock = nullptr;

    RetargetPolicy retargetPolicy = RetargetPolicy::PreserveVelocity;

    /// Optional hard ceiling on a STATEFUL (spring) animation's lifetime, in ms.
    ///
    /// A spring ignores `profile.duration` — it runs on its own physics — so the
    /// duration envelope cannot bound it and `advance()` otherwise ends it at the
    /// curve's raw `settleTime()`, itself capped only by Spring's internal 30 s.
    /// A consumer that has ALREADY resolved a lifetime for the same curve on
    /// another leg passes it here, so this animation can never OUTLIVE that leg.
    /// (It may still finish EARLIER — a spring that settles below the consumer's
    /// floor completes on its own physics — which is harmless.)
    ///
    /// The compositor needs this: its shader leg cuts a transition at
    /// `MaxAnimationDurationMs` (2 s) via `ShaderInternal::resolveTransitionLifetimeMs`,
    /// while a slider-reachable soft spring (zeta*omega < 2.649, e.g. "spring:10,0.15")
    /// settles in 3.5 s and would leave the geometry animation requesting frames
    /// for seconds after its shader was gone.
    ///
    /// The library imposes NO default: `std::nullopt` keeps the pure-physics
    /// behaviour, which is correct for consumers deliberately outside the
    /// compositor's envelope (the daemon's SurfaceAnimator). See AnimationLimits.h.
    std::optional<int> maxLifetimeMs;

    /// Fired every advance() that changes value(). Wire damage/invalidation here.
    /// When using AnimationController, prefer its virtual hooks instead.
    std::function<void(const T&)> onValueChanged;

    /// Fired once on completion. Not fired on cancel().
    std::function<void()> onComplete;
};

} // namespace PhosphorAnimation

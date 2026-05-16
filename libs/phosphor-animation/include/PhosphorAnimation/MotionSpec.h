// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <functional>

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

    /// Fired every advance() that changes value(). Wire damage/invalidation here.
    /// When using AnimationController, prefer its virtual hooks instead.
    std::function<void(const T&)> onValueChanged;

    /// Fired once on completion. Not fired on cancel().
    std::function<void()> onComplete;
};

} // namespace PhosphorAnimation

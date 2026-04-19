// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <functional>

namespace PhosphorAnimation {

class IMotionClock;

/**
 * @brief Runtime call-site bundle for starting an `AnimatedValue<T>`.
 *
 * `Profile` is the serializable config surface — duration, curve,
 * sequencing defaults — and round-trips through JSON / D-Bus / settings
 * UI. `MotionSpec<T>` is the *runtime* bundle at `start()` time,
 * combining the Profile with per-animation concerns that never belong
 * in persisted config: the clock that drives this specific animation
 * (per-output scope — see `IMotionClock`), the retarget policy default,
 * and the two user-callbacks. See Phase 3 decision H in the design doc.
 *
 * ## Ownership
 *
 * `clock` is a non-owning pointer. The clock outlives every
 * `AnimatedValue` that reads from it — in practice it is owned by the
 * compositor effect / `QQuickWindow` that spawns the animations.
 *
 * The two callbacks are held by value (std::function) and copied into
 * the AnimatedValue — they may capture local state with the usual
 * caveats (no dangling references after the capture goes out of scope).
 *
 * ## Required fields
 *
 * `profile` must carry a non-null `curve` or the curve inherited via
 * `Profile::effectiveXxx()` must resolve to something non-null through
 * the caller's inheritance chain. `clock` must be non-null. Every
 * other field is optional.
 */
template<typename T>
struct MotionSpec
{
    /// Curve + duration + sequencing metadata. Serializable.
    Profile profile;

    /// Drives this animation's progression. Non-owning; non-null required.
    IMotionClock* clock = nullptr;

    /// Default retarget behaviour for subsequent `retarget()` calls that
    /// don't specify a policy explicitly. Individual `retarget(newTo,
    /// policy)` calls override this per-invocation; the field just sets
    /// the default for `retarget(newTo)`.
    RetargetPolicy retargetPolicy = RetargetPolicy::PreserveVelocity;

    /// Fired every `advance()` that changes `value()`. The consumer's
    /// damage / invalidation hook — compositor adapters wire
    /// `effects->addRepaint(bounds)`, QML adapters wire
    /// `item->polishAndUpdate()`. Fired with the new value after the
    /// AnimatedValue has updated its cached state, so `AnimatedValue::
    /// value()` inside the callback returns the same value passed as
    /// the argument.
    std::function<void(const T&)> onValueChanged;

    /// Fired exactly once when the animation completes. After this
    /// callback returns, `isAnimating()` is false and `isComplete()` is
    /// true. Not fired on `cancel()` — that path is explicitly non-
    /// completion; consumers wanting cleanup on either path observe
    /// the two separately.
    std::function<void()> onComplete;
};

} // namespace PhosphorAnimation

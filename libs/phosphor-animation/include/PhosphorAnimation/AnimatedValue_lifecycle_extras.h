// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Auxiliary lifecycle helpers for AnimatedValue<T, Space>.
//
// This file is a continuation of <PhosphorAnimation/AnimatedValue.h> — it
// is included unconditionally from the bottom of that header so every
// existing consumer (`#include <PhosphorAnimation/AnimatedValue.h>`)
// transparently sees the full template surface. There is no need (and no
// supported path) for downstream code to include this file directly.
//
// What lives here:
//   - `rebindClock(IMotionClock*)` — clock migration with timestamp rebase.
//     Consumed by `AnimationController::advanceAnimations` when a handle
//     migrates between paint drivers (mixed-refresh-rate desktops, QML
//     scene reparenting). Not used on the hot per-frame path of typical
//     consumers, so it lives here rather than alongside `start/advance`.
//   - `seedFrom(other)` / `seedSpecFrom(other)` — cross-Space state
//     propagation for wrappers that keep parallel `AnimatedValue<T, Space>`
//     instances per `ColorSpace`. PhosphorAnimatedColor is the canonical
//     consumer; ordinary AnimatedValue users never call either method.
//
// The split exists because the combined header crossed the project's
// 800-line cap. These three helpers are conceptually peripheral to the
// core start/retarget/advance/cancel/finish lifecycle — they exist to
// support specialised wrappers (color-space dispatch) and edge cases
// (clock migration), and grouping them into a sibling keeps the core
// `AnimatedValue.h` focused on the primitives every consumer touches.
//
// Header-only template definitions: every method is a member-template of
// `AnimatedValue<T, Space>`, instantiated on demand at the call site.
// ABI-compatible with the pre-split layout because the class body shape
// (member ordering, sizes, virtual table) is unchanged.

namespace PhosphorAnimation {

/**
 * @brief Swap the driving clock without touching target, state, or
 *        interpolation.
 *
 * Used when the underlying handle migrates between paint drivers
 * mid-animation — a window moving between physical outputs on a
 * mixed-refresh-rate desktop, a QML scene re-parented to a
 * different `QQuickWindow`. The animation's progress (value,
 * velocity, elapsed) is preserved; the next `advance()` just reads
 * `dt` from the new clock instead of the old one.
 *
 * ## Epoch contract and timestamp rebase
 *
 * Per-output clocks latch `now()` independently from their own
 * paint cycles — two clocks that share the same `std::chrono::
 * steady_clock` epoch can still return *different* absolute values
 * at the same wall instant because each returns the timestamp of
 * *its* last `updatePresentTime` call. Naïvely swapping the clock
 * pointer therefore leaves `m_startTime` (latched against the old
 * clock's last-observed `now`) potentially ahead of the new
 * clock's current `now` — the next `advance()` would compute a
 * negative `elapsed`, and the stateless progression branch would
 * sample `curve->evaluate(negative-t)` and visibly rewind.
 *
 * Rebind therefore rebases `m_startTime` and `m_lastTickTime` by
 * the delta between the two clocks' current readings so that
 * `newNow - m_startTime` equals the pre-rebind `oldNow -
 * m_startTime`. Progress, not wall-time, is preserved across the
 * boundary.
 *
 * Both clocks must still share a monotonic epoch for the rebase
 * arithmetic to make sense — the shipped clocks (`CompositorClock`
 * backed by KWin's presentTime, `QtQuickClock` backed by
 * `std::chrono::steady_clock`) both derive from
 * `std::chrono::steady_clock`, so any pair is safe. A third-party
 * `IMotionClock` using a different epoch (e.g., wall-clock, a
 * domain-specific counter) must not be rebound with this method —
 * the rebase delta would mix incompatible time bases.
 *
 * Passing @p newClock == nullptr cancels the animation (same as
 * `cancel()`) — a null clock means no driver; progress cannot
 * continue. **Same owning-container stranding caveat as
 * `cancel()`** — the entry stays in the owner's map with both
 * `isAnimating()` and `isComplete()` false until explicitly
 * removed. `AnimationController::advanceAnimations` only calls
 * `rebindClock` when the per-tick resolver returns a non-null
 * clock, so this path is never hit through the controller; it
 * only matters for direct consumers of `AnimatedValue<T>`.
 *
 * No-op when the new clock equals the current clock (cheap
 * pointer compare). No callbacks fire — rebind is a pure plumbing
 * operation, distinct from retarget (which may notify per
 * `onAnimationRetargeted`).
 */
template<typename T, ColorSpace Space>
void AnimatedValue<T, Space>::rebindClock(IMotionClock* newClock)
{
    if (newClock == m_spec.clock) {
        return;
    }
    if (!newClock) {
        cancel();
        return;
    }
    // Epoch-compatibility gate. Rebase arithmetic assumes both
    // clocks share a monotonic time base — two clocks with
    // different `epochIdentity()` (or either unknown, i.e. a null
    // identity) would produce a physically meaningless delta. Log
    // once per instance and refuse the migration, leaving the
    // captured clock in place. The controller's per-tick call
    // path already guards this as well; this check is the
    // belt-and-suspenders for direct callers.
    //
    // Gate runs ANY time we have a current clock to compare against
    // — not only when m_startTime is latched. A rebind between
    // start() and the first advance() (i.e. isAnimating == true
    // but m_startTime unset) would otherwise silently install an
    // incompatible clock whose epoch identity then latches
    // permanently at the next advance; a subsequent rebind back
    // to the original clock would then rebase across incompatible
    // epochs without a defensible gate (the stored-current clock
    // by then IS the incompatible one). Checking before the
    // rebase-ability check means the captured epoch is always a
    // clock the instance deliberately accepted.
    if (m_spec.clock) {
        if (!IMotionClock::epochCompatible(m_spec.clock, newClock)) {
            if (!m_loggedEpochMismatch) {
                qCWarning(lcAnimatedValue)
                    << "rebindClock refused: epoch identity mismatch "
                    << "(old=" << m_spec.clock->epochIdentity() << ", new=" << newClock->epochIdentity()
                    << ") — rebase would corrupt progress. Keeping captured clock.";
                m_loggedEpochMismatch = true;
            }
            return;
        }
        if (m_startTime) {
            // Rebase latched timestamps by the delta between the
            // two clocks' current readings so `elapsed` and `dt`
            // survive the swap unchanged. Without this, a new
            // clock whose last-latched `now()` sits behind
            // `m_startTime` produces a negative elapsed on the
            // next stateless advance and the curve samples at t < 0.
            const auto oldNow = m_spec.clock->now();
            const auto newNow = newClock->now();
            const auto delta = newNow - oldNow;
            *m_startTime += delta;
            if (m_lastTickTime) {
                *m_lastTickTime += delta;
            }
        }
        // If m_startTime is unset (rebind between start() and
        // first advance), nothing to rebase — the first advance
        // on the new clock will latch startTime against newClock's
        // `now()` directly.
    }
    m_spec.clock = newClock;
    if (m_isAnimating) {
        // The new clock's paint loop may not be ticking this
        // handle right now; kick it so the first frame on the
        // new driver latches m_lastTickTime against a fresh
        // timestamp.
        newClock->requestFrame();
    }
}

/**
 * @brief Copy idle state (from, to, current, isComplete) from a
 *        sibling AnimatedValue that differs only in the `Space`
 *        template parameter.
 *
 * Used by wrappers that keep parallel AnimatedValue instances per
 * ColorSpace and dispatch at runtime — PhosphorAnimatedColor is
 * the canonical consumer. When the active space flips while idle,
 * the target-space instance has never seen the quiesced state from
 * the source-space instance and would read default-constructed
 * values through `from/to/value` until the next `start()`. This
 * method propagates the source's visible endpoints and current
 * interpolated value so the flip is continuous.
 *
 * Precondition: both `*this` and @p other must be idle
 * (`!isAnimating()`). A no-op if either side is animating — a
 * live segment's state must not be overwritten from the outside
 * because the callbacks wired to it (`onValueChanged`,
 * `onComplete`) are captured in a MotionSpec that the incoming
 * state does not carry. Silently no-oping on the animating path
 * matches the contract `setColorSpace` enforces at the wrapper
 * level (flip mid-animation is refused with a warning; by the
 * time seedFrom reaches an AnimatedValue the caller has already
 * gated on idle).
 *
 * Does NOT touch `m_spec` (clock, callbacks, profile) — the
 * target instance keeps its own spec for the next `start()` call.
 * The scalar `state.value` / `state.velocity` are also left
 * alone: they describe curve progression, which is meaningful
 * only during an animation, not in idle. `m_isComplete` IS
 * copied because the wrapper's `isComplete()` reads through the
 * active instance — a flip from a completed source to a
 * never-started target would otherwise drop `isComplete` from
 * true to false for no user-visible reason.
 */
template<typename T, ColorSpace Space>
template<ColorSpace OtherSpace>
void AnimatedValue<T, Space>::seedFrom(const AnimatedValue<T, OtherSpace>& other)
{
    if (m_isAnimating || other.m_isAnimating) {
        return;
    }
    m_from = other.m_from;
    m_to = other.m_to;
    m_current = other.m_current;
    m_isComplete = other.m_isComplete;
}

/**
 * @brief Copy the clock + callbacks from a sibling AnimatedValue's
 *        MotionSpec, leaving the profile alone.
 *
 * Companion to `seedFrom` for wrappers that keep parallel per-Space
 * AnimatedValue instances and need the target instance to remain
 * retarget-able after a space flip. Without this, `seedFrom` carries
 * over the visible endpoints but the target instance's
 * `m_spec.clock` is still null — `retarget()` silently rejects
 * (`if (!m_spec.clock) return false;`) because it never ran through
 * `start()` on its own.
 *
 * Copies:
 *   - `clock` — the driver for future advance() ticks.
 *   - `onValueChanged` / `onComplete` — wrapper-installed emit
 *     shims, identical across sibling instances (they re-emit on
 *     the same `this`).
 *   - `retargetPolicy` — the caller's chosen default.
 *
 * Does NOT copy `profile` — profile belongs to the next `start()`
 * call and varies per segment. Preserving the wrapper's
 * `PhosphorProfile` write-path (setProfile → next start picks it
 * up) is the intent.
 *
 * Precondition: `*this` must be idle (not animating). A live
 * segment's captured spec must not be overwritten from the outside;
 * this mirrors `seedFrom`'s idle-only contract.
 */
template<typename T, ColorSpace Space>
template<ColorSpace OtherSpace>
void AnimatedValue<T, Space>::seedSpecFrom(const AnimatedValue<T, OtherSpace>& other)
{
    if (m_isAnimating) {
        return;
    }
    m_spec.clock = other.m_spec.clock;
    m_spec.onValueChanged = other.m_spec.onValueChanged;
    m_spec.onComplete = other.m_spec.onComplete;
    m_spec.retargetPolicy = other.m_spec.retargetPolicy;
}

} // namespace PhosphorAnimation

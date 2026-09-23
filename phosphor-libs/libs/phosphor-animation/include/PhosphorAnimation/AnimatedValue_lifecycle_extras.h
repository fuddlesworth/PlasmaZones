// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Auxiliary lifecycle helpers for AnimatedValue<T, Space>: rebindClock,
// seedFrom, seedSpecFrom. Included from AnimatedValue.h — do not
// include directly.

namespace PhosphorAnimation {

/// Swap the driving clock without touching target, state, or interpolation.
/// Rebases latched timestamps so elapsed/dt survive unchanged across the swap.
/// Both clocks must share an epoch (epochCompatible); incompatible pairs are refused.
/// Passing nullptr cancels the animation.
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
    // Epoch-compatibility gate: rebase arithmetic assumes both clocks
    // share a monotonic time base. Checked even before first advance
    // to prevent silently installing an incompatible clock.
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
            const auto oldNow = m_spec.clock->now();
            const auto newNow = newClock->now();
            const auto delta = newNow - oldNow;
            *m_startTime += delta;
            if (m_lastTickTime) {
                *m_lastTickTime += delta;
            }
        }
    }
    m_spec.clock = newClock;
    if (m_isAnimating) {
        newClock->requestFrame();
    }
}

/// Copy idle state (from, to, current, isComplete) from a sibling that
/// differs only in ColorSpace. No-op if either side is animating.
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

/// Copy clock + callbacks from a sibling's MotionSpec, leaving profile alone.
/// Keeps the target instance retarget-able after a space flip. Idle-only.
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

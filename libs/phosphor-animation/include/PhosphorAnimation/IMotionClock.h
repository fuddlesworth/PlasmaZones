// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtGlobal>

#include <chrono>

namespace PhosphorAnimation {

/// Abstract clock interface for the motion runtime. Pull-model: consumers
/// read now() during their paint cycle. One clock per output/window to
/// support mixed refresh rates.
class PHOSPHORANIMATION_EXPORT IMotionClock
{
public:
    virtual ~IMotionClock() = default;

    IMotionClock(const IMotionClock&) = delete;
    IMotionClock& operator=(const IMotionClock&) = delete;
    IMotionClock(IMotionClock&&) = delete;
    IMotionClock& operator=(IMotionClock&&) = delete;

    /// Monotonically non-decreasing steady-clock reading (nanoseconds).
    virtual std::chrono::nanoseconds now() const = 0;

    /// Nominal refresh rate in Hz, or zero if unknown.
    virtual qreal refreshRate() const = 0;

    /// Schedule another paint tick. Idempotent within a single frame.
    virtual void requestFrame() = 0;

    /// Opaque epoch identity for rebindClock compatibility.
    /// nullptr (default) = incompatible with rebind onto any other clock.
    /// Clocks sharing the same non-null identity are rebase-safe.
    virtual const void* epochIdentity() const
    {
        return nullptr;
    }

    /// Shared sentinel for std::chrono::steady_clock-backed clocks.
    static const void* steadyClockEpoch();

    /// True iff both clocks are non-null and share the same non-null epochIdentity.
    static bool epochCompatible(const IMotionClock* a, const IMotionClock* b)
    {
        if (!a || !b) {
            return false;
        }
        const void* epochA = a->epochIdentity();
        const void* epochB = b->epochIdentity();
        return epochA && epochA == epochB;
    }

protected:
    IMotionClock() = default;
};

} // namespace PhosphorAnimation

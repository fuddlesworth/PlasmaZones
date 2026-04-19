// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/IMotionClock.h>

#include <QtGlobal>

#include <chrono>

namespace PhosphorAnimation::Testing {

/**
 * @brief Consumer-driven `IMotionClock` for the unit-test suite.
 *
 * Replaces the five near-identical anonymous-namespace `TestClock`
 * copies that used to live in each `test_animatedvalue_*.cpp` and
 * `test_animationcontroller.cpp`. Sharing one definition closes the
 * subtle-divergence hazard (historically the geometry/color/transform
 * copies omitted `epochIdentity()` and defaulted to null, masking
 * epoch-gate regressions that the scalar copy would have caught).
 *
 * Surface:
 * - `now()` is consumer-driven via `advanceMs()` / `setNow()`.
 * - `refreshRate()` defaults to 60 Hz; override via `setRefreshRate()`.
 * - `requestFrame()` increments a counter so tests can assert the
 *   animation kicked the driver when expected.
 * - `epochIdentity()` returns `IMotionClock::steadyClockEpoch()` by
 *   default (matches the shipped clock family). Tests that exercise
 *   the rebind-refused negative path call `setEpochIdentity(nullptr)`
 *   or pass a private sentinel.
 */
class TestClock final : public IMotionClock
{
public:
    TestClock()
        : m_epochIdentity(IMotionClock::steadyClockEpoch())
    {
    }

    std::chrono::nanoseconds now() const override
    {
        return m_now;
    }
    qreal refreshRate() const override
    {
        return m_refreshRate;
    }
    void requestFrame() override
    {
        ++m_requestFrameCalls;
    }
    const void* epochIdentity() const override
    {
        return m_epochIdentity;
    }

    void advanceMs(qreal ms)
    {
        m_now += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<qreal, std::milli>(ms));
    }
    void setNow(std::chrono::nanoseconds t)
    {
        m_now = t;
    }
    void setRefreshRate(qreal hz)
    {
        m_refreshRate = hz;
    }
    void setEpochIdentity(const void* identity)
    {
        m_epochIdentity = identity;
    }

    int requestFrameCalls() const
    {
        return m_requestFrameCalls;
    }
    void resetRequestFrameCounter()
    {
        m_requestFrameCalls = 0;
    }

private:
    std::chrono::nanoseconds m_now{0};
    qreal m_refreshRate = 60.0;
    int m_requestFrameCalls = 0;
    const void* m_epochIdentity;
};

} // namespace PhosphorAnimation::Testing

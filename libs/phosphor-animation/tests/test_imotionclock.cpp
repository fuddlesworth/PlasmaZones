// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/IMotionClock.h>

#include "TestClock.h"

#include <QTest>

#include <chrono>
#include <type_traits>

using namespace std::chrono_literals;

using PhosphorAnimation::IMotionClock;
using PhosphorAnimation::Testing::TestClock;

namespace {

/// In-test concrete clock. Consumer-controlled `now()` via `setNow`,
/// counts `requestFrame` calls, mutable refresh rate. Exists to
/// exercise the IMotionClock contract without pulling in KWin.
/// Deliberately does NOT override `epochIdentity()` — the base-class
/// default (nullptr) is part of the contract being tested, and
/// `TestClock` (used elsewhere in this file) defaults to
/// `steadyClockEpoch()` which would bypass the nullptr path entirely.
class MockClock final : public IMotionClock
{
public:
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

    void setNow(std::chrono::nanoseconds t)
    {
        m_now = t;
    }
    void setRefreshRate(qreal hz)
    {
        m_refreshRate = hz;
    }

    int requestFrameCalls() const
    {
        return m_requestFrameCalls;
    }

private:
    std::chrono::nanoseconds m_now{0};
    qreal m_refreshRate = 0.0;
    int m_requestFrameCalls = 0;
};

// A private sentinel for the "third-party non-steady monotonic source"
// test pattern — distinct address from `steadyClockEpoch()`, stable
// across the process. Shared between the epoch-mismatch tests below so
// there's one unambiguous "other sentinel" anchor rather than a per-
// test local with a drifting address.
const char kPrivateEpochTag{};

} // namespace

class TestIMotionClock : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Static contract ───

    void testCopyAndMoveAreDeleted()
    {
        // IMotionClock deliberately deletes copy + move so a clock is
        // always owned by exactly one driver. Any derived class must
        // inherit the deletion. Compile-time check via type traits.
        static_assert(!std::is_copy_constructible_v<IMotionClock>);
        static_assert(!std::is_copy_assignable_v<IMotionClock>);
        static_assert(!std::is_move_constructible_v<IMotionClock>);
        static_assert(!std::is_move_assignable_v<IMotionClock>);

        static_assert(!std::is_copy_constructible_v<MockClock>);
        static_assert(!std::is_copy_assignable_v<MockClock>);
        static_assert(!std::is_move_constructible_v<MockClock>);
        static_assert(!std::is_move_assignable_v<MockClock>);
    }

    void testAbstractCannotInstantiate()
    {
        // Compile-time: IMotionClock is abstract. Attempting to
        // instantiate it would fail; we can only instantiate concrete
        // subclasses. Runtime: upcasting a concrete clock to the
        // interface works.
        MockClock concrete;
        IMotionClock& iface = concrete;
        QCOMPARE(iface.now().count(), 0LL);
    }

    // ─── now() ───

    void testNowReturnsPushedValue()
    {
        MockClock c;
        QCOMPARE(c.now(), 0ns);

        c.setNow(std::chrono::nanoseconds(123456789));
        QCOMPARE(c.now(), std::chrono::nanoseconds(123456789));
    }

    void testNowIsConst()
    {
        // const-correctness: now() must be callable on a const
        // reference. A derived class accidentally making it non-const
        // would fail to compile against the base.
        MockClock c;
        c.setNow(42ns);
        const IMotionClock& iface = c;
        QCOMPARE(iface.now(), 42ns);
    }

    // ─── refreshRate() ───

    void testRefreshRateZeroIsUnknown()
    {
        // Zero is the documented "unknown" sentinel. Clocks backing
        // virtual / disabled outputs return zero so consumers can
        // detect the degenerate state.
        MockClock c;
        QCOMPARE(c.refreshRate(), 0.0);
    }

    void testRefreshRateReportsConfiguredValue()
    {
        MockClock c;
        c.setRefreshRate(144.0);
        QCOMPARE(c.refreshRate(), 144.0);

        c.setRefreshRate(59.94);
        QCOMPARE(c.refreshRate(), 59.94);
    }

    void testRefreshRateIsConst()
    {
        MockClock c;
        c.setRefreshRate(60.0);
        const IMotionClock& iface = c;
        QCOMPARE(iface.refreshRate(), 60.0);
    }

    // ─── requestFrame() ───

    void testRequestFrameInvokesDriver()
    {
        MockClock c;
        QCOMPARE(c.requestFrameCalls(), 0);

        c.requestFrame();
        QCOMPARE(c.requestFrameCalls(), 1);

        c.requestFrame();
        c.requestFrame();
        QCOMPARE(c.requestFrameCalls(), 3);
    }

    void testRequestFrameIsNonConst()
    {
        // requestFrame mutates driver state (schedules a tick) — it's
        // explicitly non-const. A derived class that tried to make it
        // const would be tolerated by the ABI but would mislead
        // readers; the interface shape pins the lifecycle contract.
        MockClock c;
        IMotionClock& iface = c;
        iface.requestFrame();
        QCOMPARE(c.requestFrameCalls(), 1);
    }

    // ─── Polymorphic dispatch ───

    void testPolymorphicDispatchThroughBaseReference()
    {
        // The whole point of IMotionClock: an AnimatedValue holding an
        // IMotionClock* can drive multiple concrete clock types
        // (CompositorClock, QtQuickClock, MockClock) without knowing
        // which is which. Verify dispatch actually goes through the
        // vtable.
        MockClock a;
        MockClock b;
        a.setNow(100ns);
        b.setNow(200ns);
        a.setRefreshRate(60.0);
        b.setRefreshRate(144.0);

        IMotionClock* clocks[] = {&a, &b};
        QCOMPARE(clocks[0]->now(), 100ns);
        QCOMPARE(clocks[1]->now(), 200ns);
        QCOMPARE(clocks[0]->refreshRate(), 60.0);
        QCOMPARE(clocks[1]->refreshRate(), 144.0);

        clocks[0]->requestFrame();
        QCOMPARE(a.requestFrameCalls(), 1);
        QCOMPARE(b.requestFrameCalls(), 0);
    }

    // ─── Independent instances ───

    void testTwoClocksDoNotShareState()
    {
        // Per-output scope: constructing two clocks and driving them
        // independently is the multi-monitor use case. Each instance
        // must hold its own state without cross-talk.
        MockClock a;
        MockClock b;

        a.setNow(1'000'000ns); // 1 ms
        b.setNow(16'666'667ns); // ~60 Hz frame later

        QVERIFY(a.now() != b.now());
        QCOMPARE((b.now() - a.now()).count(), 15'666'667LL);

        a.requestFrame();
        a.requestFrame();
        b.requestFrame();
        QCOMPARE(a.requestFrameCalls(), 2);
        QCOMPARE(b.requestFrameCalls(), 1);
    }

    // ─── Epoch identity + rebind compatibility contract ───

    void testDefaultEpochIdentityIsNullptr()
    {
        // The base class default is nullptr — "incompatible with rebind
        // onto any other clock" per the header contract. Concrete
        // implementations that want rebind support must override.
        MockClock c; // does not override epochIdentity
        QCOMPARE(c.epochIdentity(), static_cast<const void*>(nullptr));
    }

    void testSteadyClockEpochIsStable()
    {
        // The shared sentinel is the pointer identity by which
        // epochCompatible decides rebase-safety; its address must be
        // stable across every call and every caller (single process-
        // global static under the hood).
        const void* a = IMotionClock::steadyClockEpoch();
        const void* b = IMotionClock::steadyClockEpoch();
        QVERIFY(a != nullptr);
        QCOMPARE(a, b);
    }

    void testEpochCompatibleRejectsNull()
    {
        // Null on either side refuses the rebind — two null-identity
        // clocks opt out of the protocol even if they'd technically be
        // compatible under a type-equality check. The null path is
        // the "declined to participate" signal.
        MockClock a; // default null identity
        MockClock b; // default null identity
        QVERIFY(!IMotionClock::epochCompatible(&a, &b));
        QVERIFY(!IMotionClock::epochCompatible(nullptr, &a));
        QVERIFY(!IMotionClock::epochCompatible(&a, nullptr));
        QVERIFY(!IMotionClock::epochCompatible(nullptr, nullptr));
    }

    void testEpochCompatibleAcceptsMatchingSteadySentinels()
    {
        // Two clocks both reporting steadyClockEpoch() are rebind-safe.
        // This is the in-tree CompositorClock ↔ QtQuickClock case.
        // TestClock defaults to steadyClockEpoch().
        TestClock a;
        TestClock b;
        QVERIFY(IMotionClock::epochCompatible(&a, &b));
        QVERIFY(IMotionClock::epochCompatible(&b, &a));
    }

    void testEpochCompatibleRefusesMixedSentinels()
    {
        // A clock with a private (non-steady) sentinel must not rebase
        // against a steadyClockEpoch-backed clock even when both are
        // monotonic — the underlying tick sources are independent.
        TestClock steady; // default steadyClockEpoch
        TestClock priv;
        priv.setEpochIdentity(&kPrivateEpochTag);
        QVERIFY(!IMotionClock::epochCompatible(&steady, &priv));
        QVERIFY(!IMotionClock::epochCompatible(&priv, &steady));
    }

    void testEpochCompatibleIsReflexiveForNonNullIdentity()
    {
        // A non-null identity is compatible with itself — a degenerate
        // single-clock rebind is a no-op, not a refusal.
        TestClock a;
        QVERIFY(IMotionClock::epochCompatible(&a, &a));
    }

    void testNullIdentityClockIsNotCompatibleWithItself()
    {
        // Deliberate opt-out: a clock with null identity is incompatible
        // with every other clock including itself. Documented in the
        // epochCompatible() doc — the protocol requires an explicit
        // non-null sentinel to participate.
        MockClock a;
        QVERIFY(!IMotionClock::epochCompatible(&a, &a));
    }
};

QTEST_MAIN(TestIMotionClock)
#include "test_imotionclock.moc"

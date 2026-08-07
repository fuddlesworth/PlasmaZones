// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// StripViewAnimator: the one spring that makes a scrolling strip move as a
// rigid object instead of as N independently sprung columns.
//
// The class never dereferences a KWin::LogicalOutput* — it only uses it as a
// map key — so this suite drives it with fake output handles and a hand-driven
// clock, and compiles the implementation straight in. No compositor needed.

#include "compositor/stripviewanimator.h"

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/IMotionClock.h>

#include <QtTest>

#include <limits>
#include <memory>

using namespace PlasmaZones;

/// Hand-driven clock: the test decides what "now" is, so a leg can be stepped
/// to any point of its curve deterministically. Deliberately NOT in an
/// anonymous namespace — it is a member type of the test class below, and an
/// internal-linkage member of an external-linkage class is ill-formed enough
/// for GCC to warn (-Wsubobject-linkage).
class FakeClock final : public PhosphorAnimation::IMotionClock
{
public:
    std::chrono::nanoseconds now() const override
    {
        return m_now;
    }
    qreal refreshRate() const override
    {
        return 60.0;
    }
    void requestFrame() override
    {
        ++frameRequests;
    }
    const void* epochIdentity() const override
    {
        return IMotionClock::steadyClockEpoch();
    }

    void advanceMs(int ms)
    {
        m_now += std::chrono::milliseconds(ms);
    }

    int frameRequests = 0;

private:
    std::chrono::nanoseconds m_now{0};
};

namespace {

/// Opaque stand-ins for outputs. Never dereferenced, only compared.
KWin::LogicalOutput* fakeOutputA()
{
    return reinterpret_cast<KWin::LogicalOutput*>(0x1000);
}
KWin::LogicalOutput* fakeOutputB()
{
    return reinterpret_cast<KWin::LogicalOutput*>(0x2000);
}

} // namespace

class TestStripViewAnimator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void offsetStartsAtDeltaAndSettlesToZero();
    void secondDeltaMidFlightAccumulatesWithoutJumping();
    void outputsAreIndependent();
    void disabledPlacesOutright();
    void missingClockLeavesViewAtRest();
    void absurdDeltaIsClamped();
    void repaintsAreRequestedWhileInFlight();
    void reapingAClockDropsOnlyItsOwnOutput();
    void forgettingAnOutputDropsItsAccumulator();

private:
    std::unique_ptr<FakeClock> m_clock;
    std::unique_ptr<StripViewAnimator> m_animator;
    int m_repaints = 0;

    /// AnimatedValue latches its start time on the FIRST advance() rather than
    /// in start(), so a fresh or retargeted leg needs one tick before wall
    /// time means anything to it. The effect gets this for free — every
    /// prePaintScreen advances — but a hand-driven clock has to say it.
    void latch()
    {
        m_animator->advanceAnimations();
    }

    /// Step the clock and the leg together.
    void tick(int ms)
    {
        m_clock->advanceMs(ms);
        m_animator->advanceAnimations();
    }

    /// A short linear leg, so a step to the midpoint has an exactly
    /// predictable value — a spring's ringing would make the assertions
    /// approximate for no gain. The spring path is the library's concern and
    /// is covered by test_animatedvalue_scalar.
    void useLinearProfile(int durationMs)
    {
        // A cubic bezier through (0,0) and (1,1) IS the identity — the library
        // ships no named Linear, and building one here beats asserting against
        // a default ease's exact shape.
        auto linear = std::make_shared<PhosphorAnimation::Easing>();
        linear->x1 = 0.0;
        linear->y1 = 0.0;
        linear->x2 = 1.0;
        linear->y2 = 1.0;
        m_profile.curve = std::move(linear);
        m_profile.duration = durationMs;
    }

    /// The profile arrives per batch (the caller resolves the scrolling.view
    /// motion node), so every call site hands it in.
    void scroll(KWin::LogicalOutput* output, int deltaX)
    {
        m_animator->applyBatchDelta(output, deltaX, m_profile);
    }

    PhosphorAnimation::Profile m_profile;
};

void TestStripViewAnimator::init()
{
    m_clock = std::make_unique<FakeClock>();
    m_repaints = 0;
    m_animator = std::make_unique<StripViewAnimator>();
    m_animator->setOutputClockResolver([this](KWin::LogicalOutput*) {
        return m_clock.get();
    });
    m_animator->setRepaintRequest([this](KWin::LogicalOutput*) {
        ++m_repaints;
    });
    useLinearProfile(100);
}

void TestStripViewAnimator::offsetStartsAtDeltaAndSettlesToZero()
{
    // The contract the paint path reads: at the instant a batch lands, the
    // offset is exactly the delta, which puts every carried window back where
    // it was rendered. It then rings out to zero, where the strip agrees with
    // its committed geometry.
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);

    scroll(fakeOutputA(), 600);
    QVERIFY(m_animator->isAnimatingOn(fakeOutputA()));
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 600.0);
    latch();
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 600.0);

    tick(50);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 300.0);

    tick(50);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputA()));
    QVERIFY(!m_animator->hasActiveAnimations());
}

void TestStripViewAnimator::secondDeltaMidFlightAccumulatesWithoutJumping()
{
    // The held-arrow-key case, and the reason the animated value is the
    // ABSOLUTE view rather than the offset.
    //
    // Halfway through a 600px leg the strip is 300px behind its committed
    // geometry. A second 600px scroll lands: committed geometry moves another
    // 600px, so the strip is now 900px behind — the old remainder PLUS the new
    // step. What must NOT happen is the offset resetting to 600 (the new delta
    // alone), which is what animating the offset directly would give, because
    // that would teleport the strip forward by 300px mid-slide.
    scroll(fakeOutputA(), 600);
    latch();
    tick(50);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 300.0);

    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 900.0);

    // And it still converges rather than accumulating forever.
    latch();
    tick(500);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
}

void TestStripViewAnimator::outputsAreIndependent()
{
    // One spring PER OUTPUT: a scroll on one monitor must not offset the strip
    // on another. The paint path reads offsetFor with the window's own managed
    // output, so a shared value would drag every other strip sideways.
    scroll(fakeOutputA(), 400);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 400.0);
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputB()));

    scroll(fakeOutputB(), -200);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 400.0);
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), -200.0);

    // Dropping one output leaves the other's leg untouched.
    m_animator->forgetOutput(fakeOutputB());
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), 0.0);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 400.0);
}

void TestStripViewAnimator::disabledPlacesOutright()
{
    // With animations off a scroll must place the strip outright, which means
    // an offset of zero — never a frozen non-zero offset, which would leave
    // the whole strip permanently displaced from its committed geometry.
    m_animator->setEnabled(false);
    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputA()));

    // Disabling MID-FLIGHT must also land at zero rather than freeze.
    m_animator->setEnabled(true);
    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 600.0);
    m_animator->setEnabled(false);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
}

void TestStripViewAnimator::missingClockLeavesViewAtRest()
{
    // No clock means nothing can ever advance the leg, so starting one would
    // strand the strip at a permanent offset. Resting on the committed
    // geometry is the only safe answer.
    m_animator->setOutputClockResolver([](KWin::LogicalOutput*) -> PhosphorAnimation::IMotionClock* {
        return nullptr;
    });
    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputA()));
}

void TestStripViewAnimator::absurdDeltaIsClamped()
{
    // The wire deliberately does not validate viewDeltaX — it is a motion
    // hint, and rejecting a tile request over it would drop a valid placement
    // — so this is the only thing standing between a garbled value and a
    // strip flung somewhere it takes seconds to spring back from.
    scroll(fakeOutputA(), std::numeric_limits<int>::max());
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));

    // The rail is symmetric, and a scroll the other way is not an exotic
    // input — it is half of ordinary use.
    scroll(fakeOutputB(), std::numeric_limits<int>::min());
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), -static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));
}

void TestStripViewAnimator::repaintsAreRequestedWhileInFlight()
{
    // Nothing else damages the strip: the windows' committed geometry is
    // already final, so KWin sees no reason to repaint them while only a paint
    // offset changes.
    scroll(fakeOutputA(), 600);
    QVERIFY2(m_repaints > 0, "starting a leg must damage its output");

    latch();
    const int afterStart = m_repaints;
    tick(200);
    QVERIFY2(!m_animator->isAnimatingOn(fakeOutputA()), "the leg should have settled by now");
    QVERIFY2(m_repaints > afterStart, "the settling frame must damage its output too");
}

void TestStripViewAnimator::reapingAClockDropsOnlyItsOwnOutput()
{
    // The monitor-unplug path. The effect extracts the dying output's clock,
    // erases the map entry and reaps against the raw pointer before the
    // unique_ptr goes out of scope — an in-flight leg that kept the pointer
    // would dereference freed memory on the next prePaintScreen. A leg on a
    // surviving output must be untouched.
    auto other = std::make_unique<FakeClock>();
    m_animator->setOutputClockResolver([this, otherPtr = other.get()](KWin::LogicalOutput* output) {
        return output == fakeOutputB() ? otherPtr : static_cast<PhosphorAnimation::IMotionClock*>(m_clock.get());
    });

    scroll(fakeOutputA(), 400);
    scroll(fakeOutputB(), 400);
    latch();
    QVERIFY(m_animator->isAnimatingOn(fakeOutputA()));
    QVERIFY(m_animator->isAnimatingOn(fakeOutputB()));

    m_repaints = 0;
    const int reaped = m_animator->reapAnimationsForClock(m_clock.get());

    QCOMPARE(reaped, 1);
    QVERIFY2(!m_animator->isAnimatingOn(fakeOutputA()), "the reaped output's leg must be gone");
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
    QVERIFY2(m_repaints > 0, "reaping must damage the output whose offset it just dropped");
    QVERIFY2(m_animator->isAnimatingOn(fakeOutputB()), "another clock's leg must keep ticking");
}

void TestStripViewAnimator::forgettingAnOutputDropsItsAccumulator()
{
    // The map is keyed by LogicalOutput*, so a disconnected one must not leave
    // an entry behind: a hotplug can land a new output at the freed pointer's
    // address, and the effect calls this before the output goes away.
    //
    // Asserted against an IN-FLIGHT leg on purpose. Comparing offsets after a
    // settled one proves nothing — offsetFor reads committed minus animated,
    // and those are equal at rest whether or not the entry survived, so a
    // no-op forgetOutput would pass. A live leg is the state that differs.
    scroll(fakeOutputA(), 500);
    scroll(fakeOutputB(), 500);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(fakeOutputA()), "precondition: A's leg is mid-flight");
    QVERIFY(m_animator->offsetFor(fakeOutputA()) != 0.0);

    m_animator->forgetOutput(fakeOutputA());

    QVERIFY2(!m_animator->isAnimatingOn(fakeOutputA()), "the forgotten output must hold no leg");
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), 0.0);
    QVERIFY2(m_animator->isAnimatingOn(fakeOutputB()), "forgetting one output must not touch another");
}

QTEST_MAIN(TestStripViewAnimator)
#include "test_strip_view_animator.moc"

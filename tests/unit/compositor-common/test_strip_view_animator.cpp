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
    void offsetResolvesOntoTheOutputsOwnAxis();
    void axisForAnswersPerOutputAndOutlivesTheLeg();
    void anAxisFlipCancelsRatherThanRetargets();
    void offsetStartsAtDeltaAndSettlesToZero();
    void secondDeltaMidFlightAccumulatesWithoutJumping();
    void outputsAreIndependent();
    void disabledPlacesOutright();
    void missingClockLeavesViewAtRest();
    void absurdDeltaIsClamped();
    void repaintsAreRequestedWhileInFlight();
    void reapingAClockDropsOnlyItsOwnOutput();
    void forgettingAnOutputDropsItsAccumulator();
    void batchDeltaReportsWhetherALegStarted();

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
    ///
    /// The delta is a signed scalar ALONG @p axis, which defaults to
    /// horizontal so every pre-existing case keeps its meaning unchanged.
    /// Hands applyBatchDelta's verdict through: the bool is the SOLE input to
    /// the tiling handler's startedViewScreens set, which gates the
    /// residual-origin viewDelta branch — so the return arms are contract,
    /// not convenience, and batchDeltaReportsWhetherALegStarted pins them.
    bool scroll(KWin::LogicalOutput* output, int delta,
                PhosphorProtocol::ScrollAxis axis = PhosphorProtocol::ScrollAxis::Horizontal)
    {
        return m_animator->applyBatchDelta(output, delta, axis, m_profile);
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

/// offsetFor resolves the scalar onto the output's own axis, so a paint site
/// cannot put it in the wrong component — which is the whole reason it returns
/// a point rather than a number.
void TestStripViewAnimator::offsetResolvesOntoTheOutputsOwnAxis()
{
    scroll(fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 600.0);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), QPointF(600.0, 0.0));

    // A second output, running vertically at the same moment: the same scalar
    // resolves onto y instead. Mixed orientations coexist, which is why the
    // axis is held per output rather than globally.
    scroll(fakeOutputB(), 600, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputB()), 600.0);
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), QPointF(0.0, 600.0));

    // At rest it is a null point on either axis, not a zero in one component.
    tick(1000);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), QPointF());
    QCOMPARE(m_animator->offsetFor(fakeOutputB()), QPointF());
}

/// axisFor is not an accessor nobody reads: the strip shader pass asks it for
/// every screen it draws and binds the answer as `iStripAxis`, which is what
/// every ported pack multiplies its displacement by. A wrong answer smears the
/// whole pack along the axis the strip does not have, with no error anywhere.
///
/// The half that is easy to get wrong is the RESTING one. offsetAlongAxis and
/// offsetFor both go quiet once a leg settles, so it would look reasonable for
/// the axis to go quiet with them — but the map entry deliberately outlives the
/// leg (only forgetOutput, reset and the clock reap erase it), and the shader
/// pass keeps drawing for as long as its own fade runs. A settled vertical
/// strip that started answering Horizontal would flip the pack mid-fade.
void TestStripViewAnimator::axisForAnswersPerOutputAndOutlivesTheLeg()
{
    // Never seen: Horizontal, the historical layout and the only safe answer
    // before any batch has named an axis.
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Horizontal);
    QCOMPARE(m_animator->axisFor(nullptr), PhosphorProtocol::ScrollAxis::Horizontal);

    scroll(fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Vertical);
    // Per output, not global: a landscape monitor beside the portrait one keeps
    // its own answer, which is the whole reason the axis is held in the map.
    QCOMPARE(m_animator->axisFor(fakeOutputB()), PhosphorProtocol::ScrollAxis::Horizontal);
    scroll(fakeOutputB(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Vertical);
    QCOMPARE(m_animator->axisFor(fakeOutputB()), PhosphorProtocol::ScrollAxis::Horizontal);

    // The leg settles; the axis does not.
    tick(1000);
    QVERIFY2(!m_animator->isAnimatingOn(fakeOutputA()), "precondition: the leg has settled");
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Vertical);

    // Disconnect drops the entry, so the next answer is the never-seen one
    // again — a hotplug landing a new output at the freed address must not
    // inherit the old one's orientation.
    m_animator->forgetOutput(fakeOutputA());
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Horizontal);

    // reset() (daemon loss) clears every output's axis the same way.
    scroll(fakeOutputA(), 400, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Vertical);
    m_animator->reset();
    QCOMPARE(m_animator->axisFor(fakeOutputA()), PhosphorProtocol::ScrollAxis::Horizontal);
}

/// An axis flip under a LIVE leg cancels it. Retargeting would carry sideways
/// momentum into a vertical slide, and the accumulated view coordinate would
/// keep measuring distance along an axis the strip no longer has.
///
/// What is pinned here is the CANCEL, and only the cancel. The flip arm also
/// resets the accumulator to zero, and no assertion below can see that: the
/// accumulator's origin is arbitrary by construction (a leg always runs from
/// the previous committed value to the new one, and every reader takes the
/// DIFFERENCE `committed - animated`), so dropping `motion.committed = 0.0`
/// leaves every number in this test byte-identical. That line is unobservable
/// through the public surface rather than untested through an oversight, which
/// is why no leg is constructed for it.
void TestStripViewAnimator::anAxisFlipCancelsRatherThanRetargets()
{
    scroll(fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(fakeOutputA()), "precondition: a horizontal leg is mid-flight");

    // The rotation lands. A new batch arrives on the other axis.
    scroll(fakeOutputA(), 300, PhosphorProtocol::ScrollAxis::Vertical);
    latch();

    // The new leg runs on the new axis, and it starts from the flip rather
    // than from wherever the cancelled horizontal leg had reached: a fresh
    // 300 delta means a 300 offset, not 300 plus the old leg's remainder.
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), QPointF(0.0, 300.0));
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 300.0);

    tick(1000);
    QCOMPARE(m_animator->offsetFor(fakeOutputA()), QPointF());
}

void TestStripViewAnimator::offsetStartsAtDeltaAndSettlesToZero()
{
    // The contract the paint path reads: at the instant a batch lands, the
    // offset is exactly the delta, which puts every carried window back where
    // it was rendered. It then rings out to zero, where the strip agrees with
    // its committed geometry.
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);

    scroll(fakeOutputA(), 600);
    QVERIFY(m_animator->isAnimatingOn(fakeOutputA()));
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 600.0);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 600.0);

    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 300.0);

    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
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
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 300.0);

    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 900.0);

    // And it still converges rather than accumulating forever.
    latch();
    tick(500);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
}

void TestStripViewAnimator::outputsAreIndependent()
{
    // One spring PER OUTPUT: a scroll on one monitor must not offset the strip
    // on another. The paint path reads offsetFor with the window's own managed
    // output, so a shared value would drag every other strip sideways.
    scroll(fakeOutputA(), 400);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 400.0);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputB()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputB()));

    scroll(fakeOutputB(), -200);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 400.0);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputB()), -200.0);

    // Dropping one output leaves the other's leg untouched.
    m_animator->forgetOutput(fakeOutputB());
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputB()), 0.0);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 400.0);
}

void TestStripViewAnimator::disabledPlacesOutright()
{
    // With animations off a scroll must place the strip outright, which means
    // an offset of zero — never a frozen non-zero offset, which would leave
    // the whole strip permanently displaced from its committed geometry.
    m_animator->setEnabled(false);
    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputA()));

    // Disabling MID-FLIGHT must also land at zero rather than freeze.
    m_animator->setEnabled(true);
    scroll(fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 600.0);
    m_animator->setEnabled(false);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
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
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(fakeOutputA()));
}

void TestStripViewAnimator::absurdDeltaIsClamped()
{
    // The wire deliberately does not validate viewDelta — it is a motion
    // hint, and rejecting a tile request over it would drop a valid placement
    // — so this is the only thing standing between a garbled value and a
    // strip flung somewhere it takes seconds to spring back from.
    scroll(fakeOutputA(), std::numeric_limits<int>::max());
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));

    // The rail is symmetric, and a scroll the other way is not an exotic
    // input — it is half of ordinary use. Run this half VERTICALLY and read
    // it back through offsetFor: that is what the paint path calls, and it
    // does its own axis lookup, so a clamp applied on the scalar path alone
    // would leave the point path flinging the strip off the bottom of the
    // screen while the assertion above still passed.
    scroll(fakeOutputB(), std::numeric_limits<int>::min(), PhosphorProtocol::ScrollAxis::Vertical);
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputB()), -static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));
    QCOMPARE(m_animator->offsetFor(fakeOutputB()),
             QPointF(0.0, -static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx)));
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
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
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
    QVERIFY(m_animator->offsetAlongAxis(fakeOutputA()) != 0.0);

    m_animator->forgetOutput(fakeOutputA());

    QVERIFY2(!m_animator->isAnimatingOn(fakeOutputA()), "the forgotten output must hold no leg");
    QCOMPARE(m_animator->offsetAlongAxis(fakeOutputA()), 0.0);
    QVERIFY2(m_animator->isAnimatingOn(fakeOutputB()), "forgetting one output must not touch another");
}

void TestStripViewAnimator::batchDeltaReportsWhetherALegStarted()
{
    // The verdict is load-bearing at the caller: tiling.cpp inserts a screen
    // into startedViewScreens only when this answers true, and that set is
    // the sole gate on the residual-origin viewDelta branch — a "true" for a
    // declined leg re-origins windows against a spring that never runs, and
    // a "false" for a live one drops the residual and the strip double-moves.
    // Pin every arm.
    QVERIFY2(!scroll(fakeOutputA(), 0), "a zero delta moves nothing and starts nothing");
    QVERIFY2(!scroll(nullptr, 100), "a null output cannot host a leg");
    QVERIFY2(scroll(fakeOutputA(), 100), "a real delta with a clock starts a leg");
    QVERIFY2(scroll(fakeOutputA(), 100), "a retarget of a live leg is still a running leg");

    // Animations off: committed placement is outright, no leg to report.
    m_animator->setEnabled(false);
    QVERIFY2(!scroll(fakeOutputA(), 100), "disabled must decline — the apply path already placed outright");
    m_animator->setEnabled(true);

    // No clock for the output (hotplug race, headless harness): nothing can
    // drive the leg, so it must be declined rather than started frozen.
    m_animator->setOutputClockResolver([](KWin::LogicalOutput*) -> PhosphorAnimation::IMotionClock* {
        return nullptr;
    });
    QVERIFY2(!scroll(fakeOutputA(), 100), "a clockless output cannot start a leg");
}

QTEST_MAIN(TestStripViewAnimator)
#include "test_strip_view_animator.moc"

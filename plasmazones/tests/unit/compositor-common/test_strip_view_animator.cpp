// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// StripViewAnimator: the one spring that makes a scrolling strip move as a
// rigid object instead of as N independently sprung columns.
//
// The class never dereferences a KWin::LogicalOutput* — it only compares it
// and hands it to the clock and repaint callbacks — so this suite drives it
// with fake output handles, string strip keys and a hand-driven clock, and
// compiles the implementation straight in. No compositor needed.

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

/// The strip keys of an unsplit output A and B, and of two virtual screens
/// sharing output A.
QString stripA()
{
    return QStringLiteral("A");
}
QString stripB()
{
    return QStringLiteral("B");
}
QString leftOfA()
{
    return QStringLiteral("A/vs:0");
}
QString rightOfA()
{
    return QStringLiteral("A/vs:1");
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
    void midLegClockSwapRebindsWithoutRestarting();
    void twoStripsOnOneOutputSpringIndependently();
    void anyStripAnimatingAnswersForTheOutput();
    void forgettingAStripLeavesItsSibling();
    void forgettingAnOutputDropsEveryStripOnIt();
    void repaintFollowsTheStripsOutput();
    void axisIsPerStrip();

private:
    std::unique_ptr<FakeClock> m_clock;
    std::unique_ptr<StripViewAnimator> m_animator;
    int m_repaints = 0;
    QList<KWin::LogicalOutput*> m_repaintedOutputs;

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
    bool scroll(const QString& strip, KWin::LogicalOutput* output, int delta,
                PhosphorProtocol::ScrollAxis axis = PhosphorProtocol::ScrollAxis::Horizontal)
    {
        return m_animator->applyBatchDelta(strip, output, delta, axis, m_profile);
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
    m_animator->setRepaintRequest([this](KWin::LogicalOutput* output) {
        ++m_repaints;
        m_repaintedOutputs.append(output);
    });
    m_repaintedOutputs.clear();
    useLinearProfile(100);
}

/// offsetFor resolves the scalar onto the output's own axis, so a paint site
/// cannot put it in the wrong component — which is the whole reason it returns
/// a point rather than a number.
void TestStripViewAnimator::offsetResolvesOntoTheOutputsOwnAxis()
{
    scroll(stripA(), fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 600.0);
    QCOMPARE(m_animator->offsetFor(stripA()), QPointF(600.0, 0.0));

    // A second output, running vertically at the same moment: the same scalar
    // resolves onto y instead. Mixed orientations coexist, which is why the
    // axis is held per output rather than globally.
    scroll(stripB(), fakeOutputB(), 600, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(stripB()), 600.0);
    QCOMPARE(m_animator->offsetFor(stripB()), QPointF(0.0, 600.0));

    // At rest it is a null point on either axis, not a zero in one component.
    tick(1000);
    QCOMPARE(m_animator->offsetFor(stripA()), QPointF());
    QCOMPARE(m_animator->offsetFor(stripB()), QPointF());
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
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Horizontal);
    QCOMPARE(m_animator->axisFor(QString()), PhosphorProtocol::ScrollAxis::Horizontal);

    scroll(stripA(), fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Vertical);
    // Per output, not global: a landscape monitor beside the portrait one keeps
    // its own answer, which is the whole reason the axis is held in the map.
    QCOMPARE(m_animator->axisFor(stripB()), PhosphorProtocol::ScrollAxis::Horizontal);
    scroll(stripB(), fakeOutputB(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Vertical);
    QCOMPARE(m_animator->axisFor(stripB()), PhosphorProtocol::ScrollAxis::Horizontal);

    // The leg settles; the axis does not.
    tick(1000);
    QVERIFY2(!m_animator->isAnimatingOn(stripA()), "precondition: the leg has settled");
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Vertical);

    // Disconnect drops the entry, so the next answer is the never-seen one
    // again — a hotplug landing a new output at the freed address must not
    // inherit the old one's orientation.
    m_animator->forgetOutput(fakeOutputA());
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Horizontal);

    // reset() (daemon loss) clears every output's axis the same way.
    scroll(stripA(), fakeOutputA(), 400, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Vertical);
    m_animator->reset();
    QCOMPARE(m_animator->axisFor(stripA()), PhosphorProtocol::ScrollAxis::Horizontal);
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
    scroll(stripA(), fakeOutputA(), 600, PhosphorProtocol::ScrollAxis::Horizontal);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(stripA()), "precondition: a horizontal leg is mid-flight");

    // The rotation lands. A new batch arrives on the other axis. The flip
    // arm damages the output BEFORE cancelling (the offset the dying leg was
    // contributing vanishes with the cancel, and nothing else repaints the
    // stale frame away) — pinned by the repaint count, which deleting that
    // arm's request left unchanged before this.
    const int repaintsBeforeFlip = m_repaints;
    scroll(stripA(), fakeOutputA(), 300, PhosphorProtocol::ScrollAxis::Vertical);
    QVERIFY2(m_repaints > repaintsBeforeFlip, "the axis flip must damage the output it cancels");
    latch();

    // The new leg runs on the new axis, and it starts from the flip rather
    // than from wherever the cancelled horizontal leg had reached: a fresh
    // 300 delta means a 300 offset, not 300 plus the old leg's remainder.
    QCOMPARE(m_animator->offsetFor(stripA()), QPointF(0.0, 300.0));
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 300.0);

    tick(1000);
    QCOMPARE(m_animator->offsetFor(stripA()), QPointF());
}

void TestStripViewAnimator::offsetStartsAtDeltaAndSettlesToZero()
{
    // The contract the paint path reads: at the instant a batch lands, the
    // offset is exactly the delta, which puts every carried window back where
    // it was rendered. It then rings out to zero, where the strip agrees with
    // its committed geometry.
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);

    scroll(stripA(), fakeOutputA(), 600);
    QVERIFY(m_animator->isAnimatingOn(stripA()));
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 600.0);
    latch();
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 600.0);

    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 300.0);

    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(stripA()));
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
    scroll(stripA(), fakeOutputA(), 600);
    latch();
    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 300.0);

    scroll(stripA(), fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 900.0);

    // And it still converges rather than accumulating forever.
    latch();
    tick(500);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
}

void TestStripViewAnimator::outputsAreIndependent()
{
    // One spring PER STRIP: a scroll on one monitor must not offset the strip
    // on another. The paint path reads offsetFor with the window's own strip,
    // so a shared value would drag every other strip sideways.
    scroll(stripA(), fakeOutputA(), 400);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 400.0);
    QCOMPARE(m_animator->offsetAlongAxis(stripB()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(stripB()));

    scroll(stripB(), fakeOutputB(), -200);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 400.0);
    QCOMPARE(m_animator->offsetAlongAxis(stripB()), -200.0);

    // Dropping one output leaves the other's leg untouched.
    m_animator->forgetOutput(fakeOutputB());
    QCOMPARE(m_animator->offsetAlongAxis(stripB()), 0.0);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 400.0);
}

void TestStripViewAnimator::disabledPlacesOutright()
{
    // With animations off a scroll must place the strip outright, which means
    // an offset of zero — never a frozen non-zero offset, which would leave
    // the whole strip permanently displaced from its committed geometry.
    m_animator->setEnabled(false);
    scroll(stripA(), fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(stripA()));

    // Disabling MID-FLIGHT must also land at zero rather than freeze.
    m_animator->setEnabled(true);
    scroll(stripA(), fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 600.0);
    m_animator->setEnabled(false);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
}

void TestStripViewAnimator::missingClockLeavesViewAtRest()
{
    // No clock means nothing can ever advance the leg, so starting one would
    // strand the strip at a permanent offset. Resting on the committed
    // geometry is the only safe answer.
    m_animator->setOutputClockResolver([](KWin::LogicalOutput*) -> PhosphorAnimation::IMotionClock* {
        return nullptr;
    });
    scroll(stripA(), fakeOutputA(), 600);
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
    QVERIFY(!m_animator->isAnimatingOn(stripA()));

    // The clock can also go away UNDER a live leg (hotplug race): the
    // no-clock arm damages the output before cancelling, because the leg's
    // dying offset would otherwise stay on the last presented frame with
    // nothing scheduled to repaint it. Pinned by the repaint count, like the
    // axis flip's twin arm.
    m_animator->setOutputClockResolver([this](KWin::LogicalOutput*) {
        return m_clock.get();
    });
    scroll(stripA(), fakeOutputA(), 600);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(stripA()), "precondition: a leg is mid-flight");
    m_animator->setOutputClockResolver([](KWin::LogicalOutput*) -> PhosphorAnimation::IMotionClock* {
        return nullptr;
    });
    const int repaintsBefore = m_repaints;
    scroll(stripA(), fakeOutputA(), 100);
    QVERIFY2(m_repaints > repaintsBefore, "losing the clock under a live leg must damage the output it cancels");
    QVERIFY(!m_animator->isAnimatingOn(stripA()));
}

void TestStripViewAnimator::absurdDeltaIsClamped()
{
    // The wire deliberately does not validate viewDelta — it is a motion
    // hint, and rejecting a tile request over it would drop a valid placement
    // — so this is the only thing standing between a garbled value and a
    // strip flung somewhere it takes seconds to spring back from.
    scroll(stripA(), fakeOutputA(), std::numeric_limits<int>::max());
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));

    // The rail is symmetric, and a scroll the other way is not an exotic
    // input — it is half of ordinary use. Run this half VERTICALLY and read
    // it back through offsetFor: that is what the paint path calls, and it
    // does its own axis lookup, so a clamp applied on the scalar path alone
    // would leave the point path flinging the strip off the bottom of the
    // screen while the assertion above still passed.
    scroll(stripB(), fakeOutputB(), std::numeric_limits<int>::min(), PhosphorProtocol::ScrollAxis::Vertical);
    QCOMPARE(m_animator->offsetAlongAxis(stripB()), -static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx));
    QCOMPARE(m_animator->offsetFor(stripB()), QPointF(0.0, -static_cast<qreal>(StripViewAnimator::kMaxViewDeltaPx)));
}

void TestStripViewAnimator::repaintsAreRequestedWhileInFlight()
{
    // Nothing else damages the strip: the windows' committed geometry is
    // already final, so KWin sees no reason to repaint them while only a paint
    // offset changes.
    scroll(stripA(), fakeOutputA(), 600);
    QVERIFY2(m_repaints > 0, "starting a leg must damage its output");

    latch();
    const int afterStart = m_repaints;
    tick(200);
    QVERIFY2(!m_animator->isAnimatingOn(stripA()), "the leg should have settled by now");
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

    scroll(stripA(), fakeOutputA(), 400);
    scroll(stripB(), fakeOutputB(), 400);
    latch();
    QVERIFY(m_animator->isAnimatingOn(stripA()));
    QVERIFY(m_animator->isAnimatingOn(stripB()));

    m_repaints = 0;
    const int reaped = m_animator->reapAnimationsForClock(m_clock.get());

    QCOMPARE(reaped, 1);
    QVERIFY2(!m_animator->isAnimatingOn(stripA()), "the reaped output's leg must be gone");
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
    QVERIFY2(m_repaints > 0, "reaping must damage the output whose offset it just dropped");
    QVERIFY2(m_animator->isAnimatingOn(stripB()), "another clock's leg must keep ticking");
}

void TestStripViewAnimator::forgettingAnOutputDropsItsAccumulator()
{
    // Each strip remembers its LogicalOutput*, so a disconnected one must not
    // leave an entry behind: a hotplug can land a new output at the freed
    // pointer's address, and the effect calls this before the output goes away.
    //
    // Asserted against an IN-FLIGHT leg on purpose. Comparing offsets after a
    // settled one proves nothing — offsetFor reads committed minus animated,
    // and those are equal at rest whether or not the entry survived, so a
    // no-op forgetOutput would pass. A live leg is the state that differs.
    scroll(stripA(), fakeOutputA(), 500);
    scroll(stripB(), fakeOutputB(), 500);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(stripA()), "precondition: A's leg is mid-flight");
    QVERIFY(m_animator->offsetAlongAxis(stripA()) != 0.0);

    m_animator->forgetOutput(fakeOutputA());

    QVERIFY2(!m_animator->isAnimatingOn(stripA()), "the forgotten output must hold no leg");
    QCOMPARE(m_animator->offsetAlongAxis(stripA()), 0.0);
    QVERIFY2(m_animator->isAnimatingOn(stripB()), "forgetting one output must not touch another");
}

void TestStripViewAnimator::batchDeltaReportsWhetherALegStarted()
{
    // The verdict is load-bearing at the caller: tiling.cpp inserts a screen
    // into startedViewScreens only when this answers true, and that set is
    // the sole gate on the residual-origin viewDelta branch — a "true" for a
    // declined leg re-origins windows against a spring that never runs, and
    // a "false" for a live one drops the residual and the strip double-moves.
    // Pin every arm.
    QVERIFY2(!scroll(stripA(), fakeOutputA(), 0), "a zero delta moves nothing and starts nothing");
    QVERIFY2(!scroll(stripA(), nullptr, 100), "a null output cannot host a leg");
    QVERIFY2(!scroll(QString(), fakeOutputA(), 100), "an empty strip key cannot host a leg");
    QVERIFY2(scroll(stripA(), fakeOutputA(), 100), "a real delta with a clock starts a leg");
    QVERIFY2(scroll(stripA(), fakeOutputA(), 100), "a retarget of a live leg is still a running leg");

    // Animations off: committed placement is outright, no leg to report.
    m_animator->setEnabled(false);
    QVERIFY2(!scroll(stripA(), fakeOutputA(), 100), "disabled must decline — the apply path already placed outright");
    m_animator->setEnabled(true);

    // No clock for the output (hotplug race, headless harness): nothing can
    // drive the leg, so it must be declined rather than started frozen.
    m_animator->setOutputClockResolver([](KWin::LogicalOutput*) -> PhosphorAnimation::IMotionClock* {
        return nullptr;
    });
    QVERIFY2(!scroll(stripA(), fakeOutputA(), 100), "a clockless output cannot start a leg");
}

void TestStripViewAnimator::midLegClockSwapRebindsWithoutRestarting()
{
    // The per-tick clock re-resolution in advanceAnimations: an output whose
    // clock was rebuilt mid-leg (mode change, hotplug) must continue on the
    // NEW clock rather than keep stepping the dead one — deleting the rebind
    // kept the suite green before this.
    scroll(stripA(), fakeOutputA(), 600);
    latch();
    tick(50);
    QVERIFY2(m_animator->isAnimatingOn(stripA()), "precondition: a leg is mid-flight");
    const qreal midOffset = m_animator->offsetAlongAxis(stripA());
    QVERIFY(midOffset > 0.0);

    // A fresh clock takes over, aligned to the old one's timeline (the
    // shared steady epoch is what the rebind's compatibility gate reads).
    FakeClock replacement;
    replacement.advanceMs(50);
    m_animator->setOutputClockResolver([&replacement](KWin::LogicalOutput*) {
        return &replacement;
    });
    // One tick performs the rebind (the rebase absorbs any skew between the
    // clocks, so the offset must not jump on it)…
    m_animator->advanceAnimations();
    // …then advance ONLY the new clock: progress afterwards proves the leg
    // rebound to it rather than kept reading the old one.
    replacement.advanceMs(30);
    m_animator->advanceAnimations();
    const qreal afterSwap = m_animator->offsetAlongAxis(stripA());
    QVERIFY2(afterSwap < midOffset, "the leg must keep ringing down on the NEW clock's time");
    QVERIFY(m_animator->isAnimatingOn(stripA()));

    // And it settles on the new clock alone.
    replacement.advanceMs(1000);
    m_animator->advanceAnimations();
    QCOMPARE(m_animator->offsetFor(stripA()), QPointF());

    // Hand the resolver back to the fixture clock so the animator member
    // never holds a pointer into this frame after the slot returns.
    m_animator->setOutputClockResolver([this](KWin::LogicalOutput*) {
        return m_clock.get();
    });
}

/// Two virtual screens on one monitor are two strips: a scroll on one must not
/// slide the other's columns, which is what keying the spring by output did.
void TestStripViewAnimator::twoStripsOnOneOutputSpringIndependently()
{
    scroll(leftOfA(), fakeOutputA(), 600);
    scroll(rightOfA(), fakeOutputA(), -300);
    latch();
    tick(50);
    QCOMPARE(m_animator->offsetAlongAxis(leftOfA()), 300.0);
    QCOMPARE(m_animator->offsetAlongAxis(rightOfA()), -150.0);
    tick(1000);
    QCOMPARE(m_animator->offsetAlongAxis(leftOfA()), 0.0);
    QCOMPARE(m_animator->offsetAlongAxis(rightOfA()), 0.0);
}

/// The paint pass asks per output: any strip on it with a live leg keeps the
/// output repainting, and a strip at rest beside it says nothing.
void TestStripViewAnimator::anyStripAnimatingAnswersForTheOutput()
{
    scroll(rightOfA(), fakeOutputA(), 400);
    QVERIFY(m_animator->isAnimatingOnOutput(fakeOutputA()));
    QVERIFY(m_animator->isAnimatingOn(rightOfA()));
    QVERIFY(!m_animator->isAnimatingOn(leftOfA()));
    QVERIFY(!m_animator->isAnimatingOnOutput(fakeOutputB()));
    QVERIFY(!m_animator->isAnimatingOnOutput(nullptr));
}

/// A strip-context change on one virtual screen retires that strip's spring and
/// leaves the sibling's leg running.
void TestStripViewAnimator::forgettingAStripLeavesItsSibling()
{
    scroll(leftOfA(), fakeOutputA(), 500);
    scroll(rightOfA(), fakeOutputA(), 500);
    latch();
    tick(50);
    m_animator->forgetStrip(leftOfA());
    QVERIFY(m_animator->offsetFor(leftOfA()).isNull());
    QVERIFY(!m_animator->isAnimatingOn(leftOfA()));
    QVERIFY(!m_animator->offsetFor(rightOfA()).isNull());
    QVERIFY(m_animator->isAnimatingOn(rightOfA()));
}

/// A disconnect or a desktop switch drops every strip on the output, and only
/// those.
void TestStripViewAnimator::forgettingAnOutputDropsEveryStripOnIt()
{
    scroll(leftOfA(), fakeOutputA(), 500);
    scroll(rightOfA(), fakeOutputA(), 500);
    scroll(stripB(), fakeOutputB(), 500);
    latch();
    tick(50);
    m_animator->forgetOutput(fakeOutputA());
    QVERIFY(!m_animator->isAnimatingOn(leftOfA()));
    QVERIFY(!m_animator->isAnimatingOn(rightOfA()));
    QVERIFY(!m_animator->isAnimatingOnOutput(fakeOutputA()));
    QVERIFY(m_animator->isAnimatingOn(stripB()));
}

/// The repaint goes to the output the strip is drawn on, not to anything named
/// by the key.
void TestStripViewAnimator::repaintFollowsTheStripsOutput()
{
    scroll(rightOfA(), fakeOutputA(), 400);
    QVERIFY(!m_repaintedOutputs.isEmpty());
    for (KWin::LogicalOutput* output : std::as_const(m_repaintedOutputs)) {
        QCOMPARE(output, fakeOutputA());
    }
    m_repaintedOutputs.clear();
    latch();
    tick(1000);
    QVERIFY2(!m_repaintedOutputs.isEmpty(), "the settling frame damages the strip's output");
    for (KWin::LogicalOutput* output : std::as_const(m_repaintedOutputs)) {
        QCOMPARE(output, fakeOutputA());
    }
}

/// The axis stamp is per strip: two virtual screens on one monitor can run
/// different axes.
void TestStripViewAnimator::axisIsPerStrip()
{
    scroll(leftOfA(), fakeOutputA(), 300, PhosphorProtocol::ScrollAxis::Horizontal);
    scroll(rightOfA(), fakeOutputA(), 300, PhosphorProtocol::ScrollAxis::Vertical);
    latch();
    QCOMPARE(m_animator->axisFor(leftOfA()), PhosphorProtocol::ScrollAxis::Horizontal);
    QCOMPARE(m_animator->axisFor(rightOfA()), PhosphorProtocol::ScrollAxis::Vertical);
    QCOMPARE(m_animator->offsetFor(leftOfA()), QPointF(300.0, 0.0));
    QCOMPARE(m_animator->offsetFor(rightOfA()), QPointF(0.0, 300.0));
}

QTEST_MAIN(TestStripViewAnimator)
#include "test_strip_view_animator.moc"

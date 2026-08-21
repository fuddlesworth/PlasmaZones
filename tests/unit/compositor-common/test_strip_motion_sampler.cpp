// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// StripMotionSampler — the strip shader pass's motion arithmetic. The struct
// is plain numbers with no KWin types, so this suite drives it directly with a
// hand-rolled clock, the way test_strip_view_animator drives the animator.
//
// The four behaviours pinned here each shipped as an audit finding on the
// original inline version of this code, so each test names the failure it
// prevents rather than just the formula it checks.

#include <QtTest>

#include "transitions/stripmotionsampler.h"

using PlasmaZones::StripMotionSampler;

namespace {

// Feed `steps` frames of constant-rate motion at `dtMs` apart and return the
// sampler's last reported velocity. Starts at t = 1000 so the clock is never
// near the -1 no-baseline sentinel.
qreal runConstantRate(StripMotionSampler& s, qreal pxPerSecond, qint64 dtMs, int steps)
{
    qreal velocity = 0.0;
    qreal offset = 0.0;
    qint64 now = 1000;
    for (int i = 0; i < steps; ++i) {
        velocity = s.sampleLive(offset, now);
        offset += pxPerSecond * (qreal(dtMs) / 1000.0);
        now += dtMs;
    }
    return velocity;
}

} // namespace

class TestStripMotionSampler : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The first painted frame has no baseline to difference against, so it
    // must report zero rather than treat the whole offset as one frame's
    // worth of travel.
    void testFirstFrameReportsNoVelocity()
    {
        StripMotionSampler s;
        QCOMPARE(s.sampleLive(500.0, 1000), 0.0);
        QCOMPARE(s.timeAccumMs, qint64(0));
    }

    // Constant-rate motion converges on that rate. The one-pole filter is
    // exponential, so this checks convergence over many frames rather than
    // an exact value on any one of them.
    void testConstantRateConverges()
    {
        StripMotionSampler s;
        const qreal v = runConstantRate(s, 1200.0, 16, 60);
        QVERIFY2(std::abs(v - 1200.0) < 1.0, qPrintable(QStringLiteral("velocity was %1").arg(v)));
    }

    // The filter must LAG, not jump: one frame of a new rate cannot deliver
    // the full new velocity, or the smoothing buys nothing.
    void testSmoothingLags()
    {
        StripMotionSampler s;
        s.sampleLive(0.0, 1000);
        const qreal after = s.sampleLive(16.0, 1016); // 1000 px/s raw
        QVERIFY(after > 0.0);
        QVERIFY(after < 1000.0);
    }

    // A wheel batch steps the committed offset by a whole column in one
    // frame with no time passing. Uncompensated, the finite difference reads
    // that as an enormous velocity (and, for a leftward batch, an inverted
    // one). compensateBatchJump shifts the baseline so the difference sees
    // only spring motion.
    void testBatchJumpDoesNotSpikeVelocity()
    {
        StripMotionSampler s;
        const qreal settled = runConstantRate(s, 600.0, 16, 40);

        // The batch commits: the offset leaps by a column width with no time
        // passing, the sampler is told, and the next painted frame carries
        // one ordinary frame of the same physical motion on top.
        const qreal columnWidth = -800.0;
        s.compensateBatchJump(columnWidth);
        const qreal afterJump = s.sampleLive(600.0 * 40 * 0.016 + columnWidth, 1000 + 40 * 16);

        QVERIFY2(afterJump > 0.0, "compensated batch jump must not invert the sign");
        QVERIFY2(std::abs(afterJump - settled) < 200.0,
                 qPrintable(QStringLiteral("velocity jumped from %1 to %2").arg(settled).arg(afterJump)));
    }

    // Without compensation the same batch DOES spike, which is what makes
    // the test above meaningful rather than vacuous.
    void testUncompensatedBatchJumpSpikes()
    {
        StripMotionSampler s;
        runConstantRate(s, 600.0, 16, 40);
        const qreal afterJump = s.sampleLive(600.0 * 40 * 0.016 - 800.0, 1000 + 40 * 16);
        QVERIFY2(afterJump < 0.0, "control: an uncompensated commit step inverts the sign for a frame");
    }

    // An interval longer than the cap is a stall, a desktop-transition
    // preemption or DPMS. Dividing the accumulated displacement by the
    // CLAMPED dt would over-report the velocity by the gap ratio, so the
    // update is skipped entirely and the baseline restamped.
    void testGapSkipsUpdateAndDoesNotLeapTime()
    {
        StripMotionSampler s;
        const qreal settled = runConstantRate(s, 600.0, 16, 40);
        const qint64 timeBeforeGap = s.timeAccumMs;

        const qint64 gapMs = StripMotionSampler::kMaxVelocityDtMs * 5;
        const qreal afterGap = s.sampleLive(99999.0, 1000 + 40 * 16 + gapMs);

        QCOMPARE(afterGap, settled); // velocity untouched
        QCOMPARE(s.timeAccumMs, timeBeforeGap); // iTime did not leap by the gap

        // And the restamp means the frame after the gap differences against
        // the post-gap offset, not the pre-gap one.
        const qreal next = s.sampleLive(99999.0, 1000 + 40 * 16 + gapMs + 16);
        QVERIFY2(std::abs(next) < std::abs(settled), "post-gap frame must difference against the restamped baseline");
    }

    // iTime is the sum of painted-frame dts, so it tracks frames painted and
    // not wall clock since arming.
    void testTimeAccumulatesPaintedFramesOnly()
    {
        StripMotionSampler s;
        runConstantRate(s, 600.0, 16, 11); // 10 differenced intervals
        QCOMPARE(s.timeAccumMs, qint64(160));
    }

    // The spring is cut at a nonzero velocity, so the pass must keep
    // painting a short fade instead of stopping on a visibly-lit frame.
    void testSettleFadeHoldsThenDecaysToZero()
    {
        StripMotionSampler s;
        const qreal settled = runConstantRate(s, 1200.0, 16, 60);
        const qint64 cutMs = 1000 + 60 * 16;

        QVERIFY2(s.holdsAfterSettle(cutMs), "a fast leg cut mid-flight must hold for the fade");

        const qreal first = s.sampleSettleFade(cutMs);
        QVERIFY2(std::abs(first - settled) < 1.0, "the fade starts from the frozen live velocity");

        const qreal mid = s.sampleSettleFade(cutMs + StripMotionSampler::kSettleFadeTauMs);
        QVERIFY(std::abs(mid) < std::abs(first));

        const qreal end = s.sampleSettleFade(cutMs + StripMotionSampler::kSettleFadeMaxMs);
        QCOMPARE(end, 0.0);
        QVERIFY2(!s.holdsAfterSettle(cutMs + StripMotionSampler::kSettleFadeMaxMs), "the fade must close on its own");
    }

    // A leg that was already crawling has nothing to fade, and an entry that
    // has not painted recently is not eligible either — both settle silently
    // rather than waking the output for invisible frames.
    void testSlowAndStaleLegsDoNotFade()
    {
        StripMotionSampler slow;
        runConstantRate(slow, StripMotionSampler::kSettleVelocityEps * 0.2, 16, 40);
        QVERIFY(!slow.holdsAfterSettle(1000 + 40 * 16));

        StripMotionSampler stale;
        runConstantRate(stale, 1200.0, 16, 60);
        const qint64 cutMs = 1000 + 60 * 16;
        QVERIFY(!stale.holdsAfterSettle(cutMs + StripMotionSampler::kSettleStartWindowMs));
    }

    // The spring restarting mid-fade must resume the live path cleanly, with
    // no fade state left to reassert itself on a later settle.
    void testLiveSampleCancelsFade()
    {
        StripMotionSampler s;
        runConstantRate(s, 1200.0, 16, 60);
        const qint64 cutMs = 1000 + 60 * 16;
        s.sampleSettleFade(cutMs);
        QVERIFY(s.settleFadeStartMs >= 0);

        s.sampleLive(0.0, cutMs + 16);
        QCOMPARE(s.settleFadeStartMs, qint64(-1));
    }

    // reset() is the fresh-leg and pack-swap path: pack B must not inherit
    // pack A's clock, velocity or fade.
    void testResetClearsEveryBaseline()
    {
        StripMotionSampler s;
        runConstantRate(s, 1200.0, 16, 60);
        s.reset();
        QCOMPARE(s.timeAccumMs, qint64(0));
        QCOMPARE(s.lastPaintTimeMs, qint64(-1));
        QCOMPARE(s.smoothedVelocity, 0.0);
        QCOMPARE(s.settleFadeStartMs, qint64(-1));
        QCOMPARE(s.sampleLive(500.0, 5000), 0.0);
    }

    // Compensating before any frame has been painted must not fabricate a
    // baseline the first sampleLive would then difference against.
    void testCompensateWithoutBaselineIsNoOp()
    {
        StripMotionSampler s;
        s.compensateBatchJump(-800.0);
        QCOMPARE(s.lastOffsetPx, 0.0);
        QCOMPARE(s.sampleLive(0.0, 1000), 0.0);
    }
};

QTEST_MAIN(TestStripMotionSampler)
#include "test_strip_motion_sampler.moc"

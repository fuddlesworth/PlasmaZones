// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerUniformExtension.h>

#include <QtTest/QtTest>

using namespace PhosphorPointerShaders;

/// PointerHistory is the shared sampler behind both hosts: the compositor
/// feeds it real pointer events and the settings preview feeds it a scripted
/// path. Its liveness answer is what stops the effect costing a frame per
/// vsync forever, and its damage rect is what keeps the pass off the rest of
/// the screen, so both are contract rather than convenience.
class TestPointerHistory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testFreshHistoryIsNotLive();
    void testRingHoldsAtMostCapacityAndIsNewestFirst();
    void testStationarySamplesAreCoalesced();
    void testLivenessExpiresAfterTrailSeconds();
    void testButtonEventKeepsPassLiveWithoutMotion();
    void testVelocityDecaysWhenPointerStops();
    void testSpeedIsNotUnderReportedAtRealSamplingRates();
    void testDamageRectCoversTrailInflatedByReach();
    void testDamageRectIsEmptyWhenNotLive();
    void testPressAndReleaseAreReportedSeparately();
    void testResetClearsEverything();
};

void TestPointerHistory::testFreshHistoryIsNotLive()
{
    // Nothing has happened, so the pass must not request a single frame.
    const PointerHistory history;
    QCOMPARE(history.sampleCount(), 0);
    QVERIFY(!history.isLive(1000, 1.0));
    QVERIFY(history.damageRect(64.0, 1000, 1.0).isEmpty());

    const PointerFrameState state = history.frameState(1000, 1.0);
    QVERIFY(state.trail.isEmpty());
    // The "never happened" sentinel keeps a click pack from firing a ring at
    // session start just because the elapsed time reads as zero.
    QVERIFY(state.pressSecondsSince > 1000.0);
    QVERIFY(state.releaseSecondsSince > 1000.0);
    QVERIFY(state.idleSeconds > 1000.0);
}

void TestPointerHistory::testRingHoldsAtMostCapacityAndIsNewestFirst()
{
    PointerHistory history;
    const int overfill = PointerHistory::kCapacity + 12;
    for (int i = 0; i < overfill; ++i) {
        // 20 px apart and 16 ms apart, so every sample clears both coalescing
        // thresholds and lands in the ring.
        history.notePointer(QPointF(i * 20.0, 0.0), i * 16);
    }
    QCOMPARE(history.sampleCount(), PointerHistory::kCapacity);
    QCOMPARE(PointerHistory::kCapacity, PointerShaderContract::kMaxTrailPoints);

    const qint64 nowMs = (overfill - 1) * 16;
    const PointerFrameState state = history.frameState(nowMs, 1.0);
    QCOMPARE(state.trail.size(), PointerHistory::kCapacity);

    // Newest first is the shader-facing convention: index 0 is the pointer.
    QCOMPARE(state.trail.at(0).x(), static_cast<float>((overfill - 1) * 20.0));
    QCOMPARE(state.trail.at(0).z(), 0.0f);
    for (int i = 1; i < state.trail.size(); ++i) {
        QVERIFY(state.trail.at(i).z() > state.trail.at(i - 1).z());
        QVERIFY(state.trail.at(i).x() < state.trail.at(i - 1).x());
    }
    QCOMPARE(history.newestPosition(), QPointF((overfill - 1) * 20.0, 0.0));
}

void TestPointerHistory::testStationarySamplesAreCoalesced()
{
    // A sample is kept when the pointer has moved far enough OR when the gap
    // since the last one is long enough. Both floors have to be under the
    // threshold for an event to be dropped, so a burst of identical positions
    // inside one gap window collapses to the sample already held.
    PointerHistory history;
    history.notePointer(QPointF(100.0, 100.0), 0);
    for (qint64 t = 1; t < PointerHistory::kMinSampleGapMs; ++t) {
        history.notePointer(QPointF(100.0, 100.0), t);
    }
    QCOMPARE(history.sampleCount(), 1);

    // A sub-pixel drift is still a drift the pack should not see as motion.
    history.notePointer(QPointF(100.4, 100.4), PointerHistory::kMinSampleGapMs - 1);
    QCOMPARE(history.sampleCount(), 1);

    // Once the gap floor is cleared the sample lands even though nothing
    // moved. That floor is a minimum sampling rate, not a motion filter: it
    // is what gives a slow drag enough trail points to draw a line from.
    history.notePointer(QPointF(100.0, 100.0), PointerHistory::kMinSampleGapMs);
    QCOMPARE(history.sampleCount(), 2);

    // Moving far enough lands immediately, without waiting for the gap.
    history.notePointer(QPointF(400.0, 100.0), PointerHistory::kMinSampleGapMs + 1);
    QCOMPARE(history.sampleCount(), 3);
}

void TestPointerHistory::testLivenessExpiresAfterTrailSeconds()
{
    PointerHistory history;
    history.notePointer(QPointF(10.0, 10.0), 0);

    QVERIFY(history.isLive(0, 1.0));
    QVERIFY(history.isLive(999, 1.0));
    // At exactly trailSeconds the pack has finished fading, so the pass stops.
    QVERIFY(!history.isLive(1000, 1.0));
    QVERIFY(!history.isLive(5000, 1.0));

    // A longer-lived pack keeps its frames for longer from the same event.
    QVERIFY(history.isLive(1500, 2.0));
}

void TestPointerHistory::testButtonEventKeepsPassLiveWithoutMotion()
{
    // A click with no motion still has to drive the ripple pack, and the
    // press point is what the ring expands from.
    PointerHistory history;
    history.noteButtons(Qt::LeftButton, Qt::NoButton, QPointF(200.0, 150.0), 0);

    QVERIFY(history.isLive(500, 1.0));
    QVERIFY(!history.isLive(1200, 1.0));

    const QRectF damage = history.damageRect(50.0, 100, 1.0);
    QVERIFY(!damage.isEmpty());
    QVERIFY(damage.contains(QPointF(200.0, 150.0)));
}

void TestPointerHistory::testVelocityDecaysWhenPointerStops()
{
    PointerHistory history;
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(100.0, 0.0), 50);

    // Moving right at 100 px over 50 ms is 2000 px/s.
    const PointerFrameState moving = history.frameState(50, 1.0);
    QVERIFY(moving.velocity.x() > 0.0f);
    QCOMPARE(qRound(moving.velocity.x()), 2000);
    QCOMPARE(qRound(moving.velocity.y()), 0);

    // Once the newest sample is stale the reported velocity drops to zero,
    // so a speed-reactive pack settles instead of holding its last value.
    const PointerFrameState stopped = history.frameState(1000, 1.0);
    QCOMPARE(stopped.velocity.x(), 0.0f);
    QCOMPARE(stopped.velocity.y(), 0.0f);
    QVERIFY(stopped.idleSeconds > 0.9);
}

void TestPointerHistory::testSpeedIsNotUnderReportedAtRealSamplingRates()
{
    // The dt a speed is divided by is floored, but the distance is not, so a
    // floor at or above the real sampling interval scales every speed down by
    // interval/floor. The floor was 1/30 s, slower than any real source: a
    // 60 Hz stream reported half the true speed and a 125 Hz mouse a quarter,
    // which made every px-per-second parameter mean something else entirely
    // and left speed-gated packs drawing nothing.
    //
    // Table-driven over the rates that actually occur. The pre-existing
    // velocity test samples 50 ms apart, above the old floor, which is why it
    // never saw this.
    struct Case
    {
        const char* label;
        qint64 gapMs;
    };
    const QList<Case> cases = {
        {"1000Hz gaming mouse", 1}, {"125Hz mouse", 8}, {"60Hz stream", 16}, {"30Hz stream", 33}, {"slow 50ms", 50},
    };

    for (const Case& c : cases) {
        // Distance chosen so the true speed is always exactly 1000 px/s,
        // whatever the interval, which is what makes the rates comparable.
        const double distance = 1000.0 * (static_cast<double>(c.gapMs) / 1000.0);
        PointerHistory history;
        history.notePointer(QPointF(0.0, 0.0), 0);
        history.notePointer(QPointF(distance, 0.0), c.gapMs);

        const PointerFrameState state = history.frameState(c.gapMs, 1.0);
        QVERIFY2(qAbs(state.velocity.x() - 1000.0f) < 1.0f,
                 qPrintable(QStringLiteral("%1: reported %2 px/s for a true 1000 px/s")
                                .arg(QLatin1String(c.label))
                                .arg(static_cast<double>(state.velocity.x()))));

        // The per-sample speed the trail carries (`.w`, what a shader reads)
        // has to agree with the velocity, or a pack gating on one and drawing
        // with the other disagrees with itself.
        QVERIFY2(!state.trail.isEmpty(), c.label);
        QVERIFY2(qAbs(state.trail.first().w() - 1000.0f) < 1.0f,
                 qPrintable(QStringLiteral("%1: trail sample speed %2")
                                .arg(QLatin1String(c.label))
                                .arg(static_cast<double>(state.trail.first().w()))));
    }
}

void TestPointerHistory::testDamageRectCoversTrailInflatedByReach()
{
    PointerHistory history;
    history.notePointer(QPointF(100.0, 100.0), 0);
    history.notePointer(QPointF(300.0, 200.0), 16);

    const double reach = 40.0;
    const QRectF damage = history.damageRect(reach, 16, 1.0);
    QVERIFY(!damage.isEmpty());

    // Every live trail point plus the pack's declared reach must be inside,
    // because anything the pack paints outside this rect is never repainted.
    QVERIFY(damage.contains(QPointF(100.0, 100.0)));
    QVERIFY(damage.contains(QPointF(300.0, 200.0)));
    QVERIFY(damage.contains(QPointF(100.0 - reach + 1.0, 100.0 - reach + 1.0)));
    QVERIFY(damage.contains(QPointF(300.0 + reach - 1.0, 200.0 + reach - 1.0)));
    QVERIFY(!damage.contains(QPointF(300.0 + reach + 50.0, 200.0)));
}

void TestPointerHistory::testDamageRectIsEmptyWhenNotLive()
{
    // The cost rule: a quiet chain requests no repaint at all.
    PointerHistory history;
    history.notePointer(QPointF(100.0, 100.0), 0);
    QVERIFY(history.damageRect(40.0, 5000, 1.0).isEmpty());
}

void TestPointerHistory::testPressAndReleaseAreReportedSeparately()
{
    // Packs draw a press ring and a thinner release ring, so the two events
    // carry their own position, age and button code.
    PointerHistory history;
    history.noteButtons(Qt::RightButton, Qt::NoButton, QPointF(10.0, 20.0), 0);
    history.noteButtons(Qt::NoButton, Qt::RightButton, QPointF(30.0, 40.0), 200);

    const PointerFrameState state = history.frameState(200, 1.0);
    QCOMPARE(state.pressPos, QPointF(10.0, 20.0));
    QCOMPARE(state.pressButton, 2);
    QCOMPARE(state.pressSecondsSince, 0.2);
    QCOMPARE(state.releasePos, QPointF(30.0, 40.0));
    QCOMPARE(state.releaseButton, 2);
    QCOMPARE(state.releaseSecondsSince, 0.0);
    // Nothing is held any more, so the pressed-button mask is clear.
    QCOMPARE(state.buttons, 0);
}

void TestPointerHistory::testResetClearsEverything()
{
    // The pass resets when its chain is disabled or the output goes away; a
    // leftover trail would smear across the screen on the next engage.
    PointerHistory history;
    history.notePointer(QPointF(1.0, 2.0), 0);
    history.noteButtons(Qt::LeftButton, Qt::NoButton, QPointF(1.0, 2.0), 0);
    QVERIFY(history.isLive(10, 1.0));

    history.reset();
    QCOMPARE(history.sampleCount(), 0);
    QVERIFY(!history.isLive(10, 1.0));
    QVERIFY(history.damageRect(40.0, 10, 1.0).isEmpty());

    const PointerFrameState state = history.frameState(10, 1.0);
    QVERIFY(state.trail.isEmpty());
    QCOMPARE(state.buttons, 0);
    QVERIFY(state.pressSecondsSince > 1000.0);
}

QTEST_MAIN(TestPointerHistory)
#include "test_pointerhistory.moc"

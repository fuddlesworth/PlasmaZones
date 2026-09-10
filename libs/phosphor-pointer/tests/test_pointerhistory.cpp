// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>

#include <QtTest/QtTest>

#include <cmath>

using namespace PhosphorPointerShaders;

namespace {

/// How many samples the ring holds, read the way a host reads it: through
/// the frame state, which is the only public window onto the ring.
int sampleCount(const PointerHistory& history, qint64 nowMs)
{
    return history.frameState(nowMs, 1.0).trailSize();
}

} // namespace

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
    void testRingSpansTrailWindowAtHighEventRates();
    void testLivenessExpiresAfterTrailSeconds();
    void testButtonEventKeepsPassLiveWithoutMotion();
    void testVelocityDecaysWhenPointerStops();
    void testBackwardsTimestampReportsZeroSpeed();
    void testSameMillisecondBurstDoesNotFillTheRing();
    void testFirstMoveAfterParkingStartsFresh();
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
    QCOMPARE(sampleCount(history, 1000), 0);
    QVERIFY(!history.isLive(1000, 1.0));
    QVERIFY(history.damageRect(64.0, 1000, 1.0).isEmpty());

    const PointerFrameState state = history.frameState(1000, 1.0);
    QVERIFY(state.trailIsEmpty());
    // The "never happened" sentinel keeps a click pack from firing a ring at
    // session start just because the elapsed time reads as zero.
    QCOMPARE(state.pressSecondsSince, PointerShaderContract::kNeverSeconds);
    QCOMPARE(state.releaseSecondsSince, PointerShaderContract::kNeverSeconds);
    QCOMPARE(state.idleSeconds, PointerShaderContract::kNeverSeconds);
    QVERIFY(PointerShaderContract::kNeverSeconds > 1000.0);
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
    const qint64 nowMs = (overfill - 1) * 16;
    QCOMPARE(sampleCount(history, nowMs), PointerHistory::kCapacity);
    QCOMPARE(PointerHistory::kCapacity, PointerShaderContract::kMaxTrailPoints);

    const PointerFrameState state = history.frameState(nowMs, 1.0);
    QCOMPARE(state.trailSize(), PointerHistory::kCapacity);

    // Newest first is the shader-facing convention: index 0 is the pointer.
    QCOMPARE(state.newestTrail().x(), static_cast<float>((overfill - 1) * 20.0));
    QCOMPARE(state.trailAt(0).z(), 0.0f);
    for (int i = 1; i < state.trailSize(); ++i) {
        QVERIFY(state.trailAt(i).z() > state.trailAt(i - 1).z());
        QVERIFY(state.trailAt(i).x() < state.trailAt(i - 1).x());
    }
}

void TestPointerHistory::testStationarySamplesAreCoalesced()
{
    // A new slot is appended only once the sample interval (the 8 ms floor
    // here, with no window set) has passed since the last append. Inside it
    // an event that moved at least a pixel refreshes the head in place and a
    // sub-pixel drift is dropped, so a burst of identical positions inside
    // one interval collapses to the sample already held.
    PointerHistory history;
    history.notePointer(QPointF(100.0, 100.0), 0);
    for (qint64 t = 1; t < PointerHistory::kMinSampleGapMs; ++t) {
        history.notePointer(QPointF(100.0, 100.0), t);
    }
    QCOMPARE(sampleCount(history, PointerHistory::kMinSampleGapMs), 1);

    // A sub-pixel drift is still a drift the pack should not see as motion.
    history.notePointer(QPointF(100.4, 100.4), PointerHistory::kMinSampleGapMs - 1);
    QCOMPARE(sampleCount(history, PointerHistory::kMinSampleGapMs), 1);

    // Once the gap floor is cleared the sample lands even though nothing
    // moved. That floor is a minimum sampling rate, not a motion filter: it
    // is what gives a slow drag enough trail points to draw a line from.
    history.notePointer(QPointF(100.0, 100.0), PointerHistory::kMinSampleGapMs);
    QCOMPARE(sampleCount(history, PointerHistory::kMinSampleGapMs), 2);

    // Moving inside the gap does not append: the head follows the pointer in
    // place, so index 0 is where the pointer is without spending a slot.
    history.notePointer(QPointF(400.0, 100.0), PointerHistory::kMinSampleGapMs + 1);
    QCOMPARE(sampleCount(history, PointerHistory::kMinSampleGapMs + 1), 2);
    QCOMPARE(history.frameState(PointerHistory::kMinSampleGapMs + 1, 1.0).newestTrail().x(), 400.0f);
}

void TestPointerHistory::testRingSpansTrailWindowAtHighEventRates()
{
    // A 1000 Hz mouse. Without a window-derived interval every event took a
    // slot and the ring held 32 ms of motion: a comet's 0.5 s tail was drawn
    // from 32 ms of path and read as a stub, however long its length
    // parameter was set. With the window set the ring covers it.
    PointerHistory history;
    history.setTrailSeconds(0.8);
    QCOMPARE(history.sampleIntervalMs(), qint64(26)); // ceil(800 / 31)
    const qint64 endMs = 2000;
    for (qint64 t = 0; t <= endMs; ++t) {
        history.notePointer(QPointF(t * 2.0, 0.0), t);
    }
    const PointerFrameState state = history.frameState(endMs, 1.0);
    QCOMPARE(state.trailSize(), PointerHistory::kCapacity);
    // Head is exactly the last event, not the last appended slot.
    QCOMPARE(state.newestTrail().x(), static_cast<float>(endMs * 2.0));
    QCOMPARE(state.trailAt(0).z(), 0.0f);
    // The oldest slot reaches back at least the window.
    QVERIFY2(
        state.trailAt(PointerHistory::kCapacity - 1).z() >= 0.8f,
        qPrintable(
            QStringLiteral("oldest sample is only %1 s old").arg(state.trailAt(PointerHistory::kCapacity - 1).z())));
    // Speed on the refreshed head is still the true speed (2000 px/s), not a
    // figure divided over a stale pairing.
    QVERIFY(std::abs(state.newestTrail().w() - 2000.0f) < 50.0f);

    // The window survives a reset: an output crossing must not fall back to
    // the 8 ms floor.
    history.reset();
    QCOMPARE(history.trailSeconds(), 0.8);
    QCOMPARE(history.sampleIntervalMs(), qint64(26)); // ceil(800 / 31)

    // A short window never goes under the floor.
    PointerHistory quick;
    quick.setTrailSeconds(0.05);
    QCOMPARE(quick.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);
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

    // The hold is a strict window, like liveness and damage inclusion: at
    // exactly kVelocityHoldMs the velocity has already decayed.
    QVERIFY(history.frameState(50 + PointerHistory::kVelocityHoldMs - 1, 1.0).velocity.x() > 0.0f);
    QCOMPARE(history.frameState(50 + PointerHistory::kVelocityHoldMs, 1.0).velocity.x(), 0.0f);
}

void TestPointerHistory::testBackwardsTimestampReportsZeroSpeed()
{
    // A clock that steps back (or two events stamped identically) is not a
    // pairing anything can be divided over. Before this the negative gap
    // floored to 1 ms and a 5 px step read as 5000 px/s, which is exactly
    // the kind of spike a speed-gated pack fires on.
    PointerHistory history;
    history.notePointer(QPointF(0.0, 0.0), 1000);
    history.notePointer(QPointF(5.0, 0.0), 990);

    const PointerFrameState state = history.frameState(1000, 1.0);
    // The move is kept (the head follows it, inside the interval), but with
    // no speed.
    QCOMPARE(state.trailSize(), 1);
    QCOMPARE(state.newestTrail().x(), 5.0f);
    QCOMPARE(state.newestTrail().w(), 0.0f);
    QCOMPARE(state.velocity.x(), 0.0f);
    QCOMPARE(state.velocity.y(), 0.0f);

    // An identical stamp with a real move is the same case.
    PointerHistory same;
    same.notePointer(QPointF(0.0, 0.0), 1000);
    same.notePointer(QPointF(5.0, 0.0), 1000);
    const PointerFrameState sameState = same.frameState(1000, 1.0);
    QCOMPARE(sameState.trailSize(), 1);
    QCOMPARE(sameState.newestTrail().x(), 5.0f);
    QCOMPARE(sameState.newestTrail().w(), 0.0f);
    QCOMPARE(sameState.velocity.x(), 0.0f);
}

void TestPointerHistory::testSameMillisecondBurstDoesNotFillTheRing()
{
    // The compositor's clock is whole milliseconds and a multi-kHz mouse
    // lands several events in one of them. With a zero gap treated as an
    // append rather than a refresh, every event in the append's own
    // millisecond took a slot, and an 8 kHz stream cut the ring's window to
    // an eighth of what setTrailSeconds asked for.
    PointerHistory history;
    history.setTrailSeconds(0.8);
    const qint64 endMs = 2000;
    for (qint64 t = 0; t <= endMs; ++t) {
        for (int sub = 0; sub < 8; ++sub) {
            history.notePointer(QPointF(t * 16.0 + sub * 2.0, 0.0), t);
        }
    }
    const PointerFrameState state = history.frameState(endMs, 1.0);
    QCOMPARE(state.trailSize(), PointerHistory::kCapacity);
    QCOMPARE(state.newestTrail().x(), static_cast<float>(endMs * 16.0 + 14.0));
    QVERIFY2(
        state.trailAt(PointerHistory::kCapacity - 1).z() >= 0.8f,
        qPrintable(
            QStringLiteral("oldest sample is only %1 s old").arg(state.trailAt(PointerHistory::kCapacity - 1).z())));
}

void TestPointerHistory::testFirstMoveAfterParkingStartsFresh()
{
    // Park the pointer for ten seconds, then move 5 px. Dividing over the
    // idle gap reported 0.5 px/s for that first sample, so a pack gated on
    // speed opened one sample late. The gap past the hold is not a pairing:
    // the first move records speed 0 and the next pair carries the speed.
    PointerHistory history;
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(5.0, 0.0), 10000);

    const PointerFrameState first = history.frameState(10000, 1.0);
    QCOMPARE(first.trailSize(), 2);
    QCOMPARE(first.newestTrail().w(), 0.0f);
    QCOMPARE(first.velocity.x(), 0.0f);

    // The next sample pairs with the fresh start and reports the real speed.
    history.notePointer(QPointF(15.0, 0.0), 10010);
    const PointerFrameState second = history.frameState(10010, 1.0);
    QCOMPARE(qRound(second.newestTrail().w()), 1000);
    QCOMPARE(qRound(second.velocity.x()), 1000);

    // A gap of exactly the hold is already outside the window.
    PointerHistory edge;
    edge.notePointer(QPointF(0.0, 0.0), 0);
    edge.notePointer(QPointF(5.0, 0.0), PointerHistory::kVelocityHoldMs);
    QCOMPARE(edge.frameState(PointerHistory::kVelocityHoldMs, 1.0).newestTrail().w(), 0.0f);
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
        QVERIFY2(!state.trailIsEmpty(), c.label);
        QVERIFY2(qAbs(state.newestTrail().w() - 1000.0f) < 1.0f,
                 qPrintable(QStringLiteral("%1: trail sample speed %2")
                                .arg(QLatin1String(c.label))
                                .arg(static_cast<double>(state.newestTrail().w()))));
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
    QCOMPARE(sampleCount(history, 10), 0);
    QVERIFY(!history.isLive(10, 1.0));
    QVERIFY(history.damageRect(40.0, 10, 1.0).isEmpty());

    const PointerFrameState state = history.frameState(10, 1.0);
    QVERIFY(state.trailIsEmpty());
    QCOMPARE(state.buttons, 0);
    QCOMPARE(state.pressSecondsSince, PointerShaderContract::kNeverSeconds);
}

QTEST_MAIN(TestPointerHistory)
#include "test_pointerhistory.moc"

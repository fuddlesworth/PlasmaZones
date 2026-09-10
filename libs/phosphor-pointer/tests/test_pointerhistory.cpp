// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>

#include <QtTest/QtTest>

#include <cmath>
#include <limits>

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
    void testBackwardsOrSameStampCarriesPreviousSpeed();
    void testFilterSeedsFromTheFirstScoredSample();
    void testSameMillisecondBurstDoesNotFillTheRing();
    void testFilteredSpeedFollowsTheCurrentStrokeOnly();
    void testStationaryAppendsAreNotMotion();
    void testStrokeBoundaryIsIndependentOfTheSampleInterval();
    void testRestSlotRefreshedByAMoveBecomesMotion();
    void testSeedPositionIsNotMotion();
    void testFirstMoveAfterParkingStartsFresh();
    void testSpeedIsNotUnderReportedAtRealSamplingRates();
    void testDamageRectCoversTrailInflatedByReach();
    void testDamageRectIsEmptyWhenNotLive();
    void testPressAndReleaseAreReportedSeparately();
    void testResetClearsEverything();
    void testTrailWindowChangeMidStreamRespacesTheRing();
    void testTrailWindowEdgeValuesFallBackToTheFloor();
    void testClockStepDoesNotLatchAcrossFollowingEvents();
    void testRestingPointerEventuallyEvictsEveryMotionSample();
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

void TestPointerHistory::testFilterSeedsFromTheFirstScoredSample()
{
    // A short stroke after a park, where the seed choice moves the answer
    // by hundreds of px/s: the start is scored 0 and must be the boundary,
    // the first scored sample the seed. Seeding from the 0 would give 92
    // here; seeding from the first scored sample gives 100*0.54 + 200*0.46.
    PointerHistory history;
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(1000.0, 0.0), 5000); // park, then the start (speed 0)
    history.notePointer(QPointF(1005.0, 0.0), 5050); // 100 px/s
    history.notePointer(QPointF(1015.0, 0.0), 5100); // 200 px/s
    const PointerFrameState state = history.frameState(5100, 1.0);
    QCOMPARE(state.trailSize(), 4);
    QCOMPARE(state.trailAt(2).w(), 0.0f); // the start
    QVERIFY2(std::abs(state.filteredSpeed - 146.0) < 1.0,
             qPrintable(QStringLiteral("filtered %1, expected 146").arg(state.filteredSpeed)));

    // A start that a later in-interval event refreshed carries that event's
    // scored speed and seeds: at a 4 s window (130 ms interval) the start
    // slot at 5000 is refreshed by the event at 5050, so the ring holds one
    // stroke slot scored 100 px/s plus the appended 5150 sample.
    PointerHistory refreshed;
    refreshed.setTrailSeconds(4.0);
    refreshed.notePointer(QPointF(0.0, 0.0), 0);
    refreshed.notePointer(QPointF(1000.0, 0.0), 5000);
    refreshed.notePointer(QPointF(1005.0, 0.0), 5050);
    // 30 px over 90 ms (inside the hold, so it pairs) at 140 ms from the
    // slot's append (past the interval, so it appends): 333 px/s.
    refreshed.notePointer(QPointF(1035.0, 0.0), 5140);
    const PointerFrameState two = refreshed.frameState(5140, 1.0);
    QCOMPARE(two.trailSize(), 3);
    QCOMPARE(qRound(two.trailAt(1).w()), 100);
    const double expected = 100.0 * 0.54 + (30.0 / 0.09) * 0.46;
    QVERIFY2(std::abs(two.filteredSpeed - expected) < 1.0,
             qPrintable(QStringLiteral("filtered %1, expected %2").arg(two.filteredSpeed).arg(expected)));
}

void TestPointerHistory::testBackwardsOrSameStampCarriesPreviousSpeed()
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

    // With a real speed on the previous event the stamp CARRIES it and
    // leaves the pairing where it was (a stepped-back stamp is the same case
    // as a shared millisecond; both real clocks are monotonic). A reset to
    // zero here would pass the two cases above by coincidence.
    PointerHistory carried;
    carried.notePointer(QPointF(0.0, 0.0), 0);
    carried.notePointer(QPointF(100.0, 0.0), 50); // 2000 px/s
    carried.notePointer(QPointF(105.0, 0.0), 40); // stepped back
    const PointerFrameState carriedState = carried.frameState(50, 1.0);
    QCOMPARE(carriedState.newestTrail().x(), 105.0f);
    QCOMPARE(qRound(carriedState.newestTrail().w()), 2000);
    QCOMPARE(qRound(carriedState.velocity.x()), 2000);
    // And the idle clock did not rewind to the stepped-back stamp: at the
    // frame of the last real event it still reads 0, not 10 ms.
    QCOMPARE(carriedState.idleSeconds, 0.0);
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
    // The eight events in one millisecond cover 16 px, so the true speed is
    // 16000 px/s. An event in the same millisecond as the previous one has
    // no dt to divide over; it carries the previous speed and leaves the
    // pairing where it was, so the next millisecond's first event pairs
    // across the whole 16 px and reads the true figure, where scoring each
    // same-millisecond event as 0 left a zero on the head most of the time.
    QVERIFY2(std::abs(state.newestTrail().w() - 16000.0f) < 100.0f,
             qPrintable(QStringLiteral("head speed %1").arg(state.newestTrail().w())));
    QVERIFY2(std::abs(state.velocity.x() - 16000.0f) < 100.0f,
             qPrintable(QStringLiteral("velocity %1").arg(state.velocity.x())));
    QVERIFY2(std::abs(state.filteredSpeed - 16000.0) < 100.0,
             qPrintable(QStringLiteral("filtered %1").arg(state.filteredSpeed)));
    // The pairing is the FIRST event of the last millisecond (the seven that
    // shared it carried its speed and did not become the pairing): one more
    // event 1 ms on, 2 px past the last position, scores over the 16 px from
    // that first event, not the 2 px from the last one.
    history.notePointer(QPointF(endMs * 16.0 + 16.0, 0.0), endMs + 1);
    const PointerFrameState next = history.frameState(endMs + 1, 1.0);
    QVERIFY2(std::abs(next.newestTrail().w() - 16000.0f) < 100.0f,
             qPrintable(QStringLiteral("speed after the burst %1").arg(next.newestTrail().w())));
}

void TestPointerHistory::testFilteredSpeedFollowsTheCurrentStrokeOnly()
{
    // A fast stroke, a pause, then a slow nudge. The ring is never purged on
    // rest, so the fast stroke's samples are still in it; the filter must
    // not seed from them, or a speed-gated pack opens on the nudge as if the
    // pointer were still flying.
    PointerHistory history;
    history.setTrailSeconds(0.8);
    qint64 t = 0;
    for (; t <= 500; t += 10) {
        history.notePointer(QPointF(t * 2.0, 0.0), t); // 2000 px/s
    }
    const PointerFrameState flying = history.frameState(t - 10, 1.0);
    QVERIFY2(std::abs(flying.filteredSpeed - 2000.0) < 50.0,
             qPrintable(QStringLiteral("filtered while flying %1").arg(flying.filteredSpeed)));

    // Parked past the hold, then one slow move: 5 px over 50 ms is 100 px/s
    // on the second event; the first move after a park scores 0.
    t += 3000;
    history.notePointer(QPointF(1005.0, 0.0), t);
    history.notePointer(QPointF(1010.0, 0.0), t + 50);
    const PointerFrameState nudged = history.frameState(t + 50, 1.0);
    // Exactly the nudge's own speed: the stroke's first sample carries 0 by
    // construction and is the boundary, not the seed, or a short stroke
    // would read as under half of its speed.
    QVERIFY2(std::abs(nudged.filteredSpeed - 100.0) < 1.0,
             qPrintable(QStringLiteral("filtered after a park %1 (stale stroke or the zero start seeded it)")
                            .arg(nudged.filteredSpeed)));
}

void TestPointerHistory::testRestSlotRefreshedByAMoveBecomesMotion()
{
    // Rest past the interval (a non-motion slot lands), then a move inside
    // the next interval: the rest slot is refreshed into a motion sample
    // without spending a slot, the move counts (idle resets, the filter sees
    // it), and the next append still measures its gap from that slot.
    PointerHistory history;
    history.setTrailSeconds(0.8); // 26 ms interval
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(100.0, 0.0), 50);
    history.notePointer(QPointF(100.0, 0.0), 80); // rest slot, not motion
    QCOMPARE(sampleCount(history, 80), 3);
    QVERIFY(history.frameState(80, 1.0).idleSeconds > 0.02);

    history.notePointer(QPointF(110.0, 0.0), 90); // inside the rest slot's interval
    const PointerFrameState moved = history.frameState(90, 1.0);
    QCOMPARE(moved.trailSize(), 3);
    QCOMPARE(moved.newestTrail().x(), 110.0f);
    QCOMPARE(moved.idleSeconds, 0.0);
    // 10 px over the 40 ms since the last accepted event (the 50 ms one).
    QCOMPARE(qRound(moved.newestTrail().w()), 250);
    QVERIFY(moved.filteredSpeed > 0.0);

    // The gap is still measured from the rest slot's append at 80 ms, so an
    // event at 105 ms (25 ms later) refreshes and one at 106 ms appends.
    history.notePointer(QPointF(120.0, 0.0), 105);
    QCOMPARE(sampleCount(history, 105), 3);
    history.notePointer(QPointF(130.0, 0.0), 106);
    QCOMPARE(sampleCount(history, 106), 4);
}

void TestPointerHistory::testSeedPositionIsNotMotion()
{
    // The compositor seeds an empty ring from a buttons-only event so the
    // first live frame has a pointer, without a click counting as a move.
    PointerHistory history;
    history.seedPosition(QPointF(40.0, 50.0), 1000);
    QCOMPARE(sampleCount(history, 1000), 1);
    const PointerFrameState state = history.frameState(1000, 1.0);
    QCOMPARE(state.newestTrail().x(), 40.0f);
    QCOMPARE(state.idleSeconds, PointerShaderContract::kNeverSeconds);
    QCOMPARE(state.filteredSpeed, 0.0);
    QVERIFY(!history.isLive(1000, 1.0));
    // A ring with samples is left alone.
    history.seedPosition(QPointF(0.0, 0.0), 1001);
    QCOMPARE(history.frameState(1001, 1.0).newestTrail().x(), 40.0f);
    // The compositor's real path after a click: a move INSIDE the seed slot's
    // interval refreshes the seeded slot into motion (no slot spent, the
    // head follows, the idle clock starts, the velocity has no pairing yet)
    // and keeps the slot's append time as its anchor.
    history.notePointer(QPointF(45.0, 50.0), 1004);
    const PointerFrameState refreshed = history.frameState(1004, 1.0);
    QCOMPARE(refreshed.trailSize(), 1);
    QCOMPARE(refreshed.newestTrail().x(), 45.0f);
    QCOMPARE(refreshed.idleSeconds, 0.0);
    QCOMPARE(refreshed.velocity.x(), 0.0f);
    QVERIFY(history.isLive(1004, 1.0));
    // The anchor stayed at the seed time, so 8 ms after the SEED appends.
    history.notePointer(QPointF(50.0, 50.0), 1008);
    QCOMPARE(sampleCount(history, 1008), 2);
}

void TestPointerHistory::testStationaryAppendsAreNotMotion()
{
    // The preview feeds the sampler every tick even while its pointer rests,
    // so a same-position sample lands whenever the interval has passed (the
    // ring's minimum sampling rate). It is not motion: the idle clock keeps
    // running and the velocity pairing stays put, so a resting pointer idles
    // in the preview exactly as on the compositor, which sends nothing.
    PointerHistory history;
    history.setTrailSeconds(0.5);
    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(100.0, 0.0), 50);
    for (qint64 t = 66; t <= 1050; t += 16) {
        history.notePointer(QPointF(100.0, 0.0), t);
    }
    const PointerFrameState moving = history.frameState(50, 1.0);
    const PointerFrameState resting = history.frameState(1050, 1.0);
    QVERIFY2(resting.trailSize() > 2, "the stationary samples still land in the ring");
    QVERIFY2(resting.idleSeconds >= 1.0, qPrintable(QStringLiteral("idle %1").arg(resting.idleSeconds)));
    QVERIFY(!history.isLive(1050, 0.5));
    QCOMPARE(resting.velocity.x(), 0.0f);
    // The stationary samples are skipped by the filter, so the figure HOLDS
    // across the rest, as it does on the compositor where no sample lands at
    // all; the packs' idle fades end the drawing, not a decaying gate.
    QCOMPARE(resting.filteredSpeed, moving.filteredSpeed);
    QVERIFY(resting.filteredSpeed > 0.0);

    // The pairing stayed put too: the next real move pairs with the 50 ms
    // event, a gap past the hold, so it scores 0 and starts a stroke. Had a
    // stationary append become the pairing, this would read about 600 px/s.
    history.notePointer(QPointF(110.0, 0.0), 1066);
    const PointerFrameState moved = history.frameState(1066, 1.0);
    QCOMPARE(moved.newestTrail().w(), 0.0f);
    QCOMPARE(moved.velocity.x(), 0.0f);
    QCOMPARE(moved.filteredSpeed, 0.0);
}

void TestPointerHistory::testStrokeBoundaryIsIndependentOfTheSampleInterval()
{
    // A long window spaces the ring's slots past the velocity hold (4 s is
    // ceil(4000 / 31) = 130 ms between appends), so a stroke boundary read
    // from the gap between SLOTS would end the stroke at every slot and the
    // filter would collapse to the raw head speed. The boundary is marked at
    // the EVENT that follows a park instead, so a steady stroke of
    // alternating speeds filters over its whole run.
    PointerHistory history;
    history.setTrailSeconds(4.0);
    QVERIFY(history.sampleIntervalMs() >= PointerHistory::kVelocityHoldMs);
    qint64 t = 0;
    double x = 0.0;
    for (int i = 0; i < 400; ++i, t += 10) {
        // 2 px then 30 px every 10 ms: 200 and 3000 px/s alternating.
        x += (i % 2 == 0) ? 2.0 : 30.0;
        history.notePointer(QPointF(x, 0.0), t);
    }
    const PointerFrameState state = history.frameState(t - 10, 1.0);
    const double head = state.newestTrail().w();
    QVERIFY2(std::abs(state.filteredSpeed - head) > 300.0,
             qPrintable(QStringLiteral("filtered %1 collapsed to the head's %2").arg(state.filteredSpeed).arg(head)));
    // Pinned to the walk itself: the exponential filter (a = 0.46) over the
    // slots' speeds, oldest to newest, exactly as the frame state exposes
    // them. The ring's oldest slot is the stroke's start (the first event,
    // scored 0), which is the boundary and not the seed, so the walk seeds
    // from the slot before it. The seed rule itself is pinned by
    // testFilterSeedsFromTheFirstScoredSample on a ring short enough for the
    // choice to matter; here the difference has decayed below the tolerance.
    const int last = state.trailSize() - 1;
    const int seed = state.trailAt(last).w() <= 0.0f ? last - 1 : last;
    double expected = state.trailAt(seed).w();
    for (int i = seed - 1; i >= 0; --i) {
        expected = expected * 0.54 + state.trailAt(i).w() * 0.46;
    }
    QVERIFY2(std::abs(state.filteredSpeed - expected) < 1.0,
             qPrintable(QStringLiteral("filtered %1, walk gives %2").arg(state.filteredSpeed).arg(expected)));
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

void TestPointerHistory::testTrailWindowChangeMidStreamRespacesTheRing()
{
    // rebuildChain sets the window on any profile or registry change WITHOUT
    // resetting, so a populated ring really does carry slots anchored under
    // the old interval into the new one. Nothing covered that.
    PointerHistory history;
    history.setTrailSeconds(0.8);
    QCOMPARE(history.sampleIntervalMs(), qint64(26)); // ceil(800 / 31)

    qint64 t = 0;
    for (int i = 0; i < 40; ++i) {
        t += 30;
        history.notePointer(QPointF(double(i) * 20.0, 0.0), t);
    }
    QCOMPARE(history.frameState(t, 0.8).trailSize(), PointerShaderContract::kMaxTrailPoints);

    // Grow the window. The interval widens, and the slots already in the ring
    // keep their absolute anchors, so the next append simply waits longer.
    history.setTrailSeconds(4.0);
    QCOMPARE(history.sampleIntervalMs(), qint64(130)); // ceil(4000 / 31)

    const int before = history.frameState(t, 4.0).trailSize();
    t += 40; // inside the new interval, past the old one
    history.notePointer(QPointF(2000.0, 0.0), t);
    QCOMPARE(history.frameState(t, 4.0).trailSize(), before);
    // The head still follows the pointer exactly, which is the invariant the
    // in-place refresh exists to keep.
    QCOMPARE(history.frameState(t, 4.0).newestTrail().x(), 2000.0f);

    // And once a full new interval has passed it appends again.
    t += 130;
    history.notePointer(QPointF(2200.0, 0.0), t);
    const PointerFrameState grown = history.frameState(t, 4.0);
    QCOMPARE(grown.newestTrail().x(), 2200.0f);
    QVERIFY(grown.trailAt(1).x() == 2000.0f);
}

void TestPointerHistory::testTrailWindowEdgeValuesFallBackToTheFloor()
{
    PointerHistory history;
    // The empty-chain path passes exactly this.
    history.setTrailSeconds(0.0);
    QCOMPARE(history.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);

    history.setTrailSeconds(-5.0);
    QCOMPARE(history.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);

    // Exported API, so the cast in setTrailSeconds has to stay defined for a
    // caller that is not the JSON boundary.
    // Asserted on the STORED window, not on the interval. Without the isfinite
    // guard the cast is undefined, and the value it happens to produce on
    // x86-64 (INT64_MIN) still floors to kMinSampleGapMs — so an
    // interval-only assertion passes on the broken code on the very platform
    // CI runs.
    history.setTrailSeconds(std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(history.trailSeconds(), 0.0);
    QCOMPARE(history.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);

    // Infinity is not finite, so it takes the same rejection arm as NaN
    // rather than clamping to kMaxTrailSeconds.
    history.setTrailSeconds(std::numeric_limits<double>::infinity());
    QCOMPARE(history.trailSeconds(), 0.0);
    QCOMPARE(history.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);

    // The upper clamp is what a large FINITE value gets.
    history.setTrailSeconds(1.0e9);
    QCOMPARE(history.trailSeconds(), PointerHistory::kMaxTrailSeconds);

    // A window whose interval would fall under the floor still gets the floor.
    history.setTrailSeconds(0.05);
    QCOMPARE(history.sampleIntervalMs(), PointerHistory::kMinSampleGapMs);
}

void TestPointerHistory::testClockStepDoesNotLatchAcrossFollowingEvents()
{
    // A stamp a little behind the last one is two events sharing a tick and
    // deliberately does NOT advance the pairing. A stamp far behind it is a
    // clock discontinuity, and treating that the same way latched: the pairing
    // never moved, so EVERY event for the length of the step kept measuring
    // against the same frozen stamp, none was accepted, the idle clock stood
    // still and the trail expired under a moving pointer.
    //
    // The single-event case cannot show this. It needs the repeat.
    PointerHistory history;
    history.setTrailSeconds(1.0);
    history.notePointer(QPointF(0.0, 0.0), 10000);
    history.notePointer(QPointF(100.0, 0.0), 10050);
    QVERIFY(history.isLive(10050, 1.0));

    // The clock steps back a full second, then the pointer keeps moving. The
    // whole run stays BELOW the pre-step stamp of 10050 on purpose: the latch
    // only exists while the new clock is behind the frozen one, so letting the
    // timeline overtake it lets the broken code recover on its own and the
    // test stops discriminating.
    qint64 t = 9000;
    for (int i = 1; i <= 20; ++i) {
        t += 40;
        history.notePointer(QPointF(double(i) * 30.0, 0.0), t);
    }
    QVERIFY(t < 10050);
    // On the new timeline the pointer has been moving continuously, so the
    // pass must still be live. Before the re-anchor this read as idle since
    // 10050 and went dark.
    // The RING SIZE is the discriminator. isLive and idleSeconds cannot be:
    // a latched idle anchor sits in the FUTURE relative to the stepped-back
    // clock, so both of them report a healthy trail on the broken code too.
    // They are asserted below as corroboration, not as the detection.
    const PointerFrameState state = history.frameState(t, 1.0);
    QVERIFY2(state.trailSize() > 2,
             "the events after a clock step were never accepted: the pairing stayed frozen and the head "
             "refreshed in place instead of the ring advancing");
    QVERIFY(history.isLive(t, 1.0));
    QCOMPARE(state.idleSeconds, 0.0);
    QCOMPARE(state.newestTrail().x(), 600.0f);
}

void TestPointerHistory::testRestingPointerEventuallyEvictsEveryMotionSample()
{
    // The rest slots are appended one per interval, so a long enough rest
    // rotates every motion sample out of a fixed-size ring and the filtered
    // speed has nothing left to report. This pins that boundary rather than
    // stopping one slot short of it, which is where the neighbouring test
    // deliberately sits.
    PointerHistory history;
    history.setTrailSeconds(0.5);
    const qint64 interval = history.sampleIntervalMs();

    history.notePointer(QPointF(0.0, 0.0), 0);
    history.notePointer(QPointF(100.0, 0.0), 50);
    QVERIFY(history.frameState(50, 0.5).filteredSpeed > 0.0);

    // Rest for more than a ring's worth of intervals.
    qint64 t = 50;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints + 2; ++i) {
        t += interval;
        history.notePointer(QPointF(100.0, 0.0), t);
    }
    QCOMPARE(history.frameState(t, 0.5).filteredSpeed, 0.0);
}

QTEST_MAIN(TestPointerHistory)
#include "test_pointerhistory.moc"

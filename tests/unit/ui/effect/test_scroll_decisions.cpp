// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the scroll-managed window decision helpers
// (tilinghandler/scrolldecisions.h) — the windowed-fullscreen 5-way batch
// decision (with its clear-in-flight marker arm/consume contract), the
// column-maximize 3-way batch decision, and the counter-assert burst
// budget. Same header-only reach as test_anchor_uniforms:
// kwin-effect has no linkable test target, so the pure halves are extracted
// into a header this test includes directly.

#include <tilinghandler/scrolldecisions.h>

#include <QTest>

using namespace PlasmaZones::ScrollDecisions;

class TestScrollDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The full 16-row truth table over (flagOnWire, inSet,
    // requestedFullscreen, clearInFlight). Data-driven with per-row QCOMPARE
    // so a failing row does not mask the rest.
    void windowedFullscreenTruthTable_data()
    {
        QTest::addColumn<bool>("flagOnWire");
        QTest::addColumn<bool>("inSet");
        QTest::addColumn<bool>("requested");
        QTest::addColumn<bool>("marker");
        QTest::addColumn<int>("expectedAction");
        QTest::addColumn<bool>("expectedConsume");

        const auto a = [](WfsAction act) {
            return static_cast<int>(act);
        };
        // flag=0, not held: untouched. The marker is consumed exactly when
        // it was armed (the flag-off entry is the clear's authoritative
        // echo); requested state is irrelevant.
        QTest::newRow("off/free/req0/mark0") << false << false << false << false << a(WfsAction::None) << false;
        QTest::newRow("off/free/req0/mark1") << false << false << false << true << a(WfsAction::None) << true;
        QTest::newRow("off/free/req1/mark0") << false << false << true << false << a(WfsAction::None) << false;
        QTest::newRow("off/free/req1/mark1") << false << false << true << true << a(WfsAction::None) << true;
        // flag=0, held: release, marker consumed when armed.
        QTest::newRow("off/held/req0/mark0") << false << true << false << false << a(WfsAction::Release) << false;
        QTest::newRow("off/held/req0/mark1") << false << true << false << true << a(WfsAction::Release) << true;
        QTest::newRow("off/held/req1/mark0") << false << true << true << false << a(WfsAction::Release) << false;
        QTest::newRow("off/held/req1/mark1") << false << true << true << true << a(WfsAction::Release) << true;
        // flag=1, not held: adopt — unless the armed marker refuses it (a
        // batch emitted before the daemon processed our clear must not
        // re-fullscreen the window the user just exited). Never consumes.
        QTest::newRow("on/free/req0/mark0") << true << false << false << false << a(WfsAction::Adopt) << false;
        QTest::newRow("on/free/req0/mark1") << true << false << false << true << a(WfsAction::None) << false;
        QTest::newRow("on/free/req1/mark0") << true << false << true << false << a(WfsAction::Adopt) << false;
        QTest::newRow("on/free/req1/mark1") << true << false << true << true << a(WfsAction::None) << false;
        // flag=1, held: requested discriminates steady-state refresh from
        // the deferred client-exit reconcile (committed lag must not read
        // as an exit — hence requested, not committed). Marker irrelevant.
        QTest::newRow("on/held/req0/mark0")
            << true << true << false << false << a(WfsAction::DeferredReconcile) << false;
        QTest::newRow("on/held/req0/mark1")
            << true << true << false << true << a(WfsAction::DeferredReconcile) << false;
        QTest::newRow("on/held/req1/mark0") << true << true << true << false << a(WfsAction::Refresh) << false;
        QTest::newRow("on/held/req1/mark1") << true << true << true << true << a(WfsAction::Refresh) << false;
    }

    void windowedFullscreenTruthTable()
    {
        QFETCH(bool, flagOnWire);
        QFETCH(bool, inSet);
        QFETCH(bool, requested);
        QFETCH(bool, marker);
        QFETCH(int, expectedAction);
        QFETCH(bool, expectedConsume);

        const WfsDecision d = resolveWindowedFullscreenAction(flagOnWire, inSet, requested, marker);
        QCOMPARE(static_cast<int>(d.action), expectedAction);
        QCOMPARE(d.consumeClearMarker, expectedConsume);
    }

    // The arm/consume repeat contract in miniature: a lost clear's marker
    // refuses re-adoption while armed, and any flag-off echo (success) or
    // the caller's error-arm drop makes the next flagged batch adopt again.
    void markerRefusesAdoptionUntilConsumed()
    {
        bool marker = true;
        // Batch 1 (emitted before the daemon processed the clear): flagged,
        // not held — refused.
        WfsDecision d = resolveWindowedFullscreenAction(true, false, false, marker);
        QCOMPARE(static_cast<int>(d.action), static_cast<int>(WfsAction::None));
        // The clear succeeded: batch 2 carries flag-off. Marker consumed.
        d = resolveWindowedFullscreenAction(false, false, false, marker);
        QVERIFY(d.consumeClearMarker);
        marker = false;
        // Batch 3: a genuine re-flag now adopts.
        d = resolveWindowedFullscreenAction(true, false, false, marker);
        QCOMPARE(static_cast<int>(d.action), static_cast<int>(WfsAction::Adopt));
    }

    // The full 8-row truth table over (flagOnWire, inSet, kwinMaximized).
    void columnMaximizeTruthTable_data()
    {
        QTest::addColumn<bool>("flagOnWire");
        QTest::addColumn<bool>("inSet");
        QTest::addColumn<bool>("kwinMaximized");
        QTest::addColumn<int>("expectedAction");

        const auto a = [](MaximizeAction act) {
            return static_cast<int>(act);
        };
        // Flag off, not a member: nothing to do, whatever KWin's own bit
        // says. A window the USER maximized on a non-maximized column lands
        // here, and must be left alone — the batch's anti-ballooning clear
        // owns that case, not this decision.
        QTest::newRow("off/free/kwin0") << false << false << false << a(MaximizeAction::None);
        QTest::newRow("off/free/kwin1") << false << false << true << a(MaximizeAction::None);
        // Flag off while held: the engine dropped the maximize, so hand the
        // bit back. Release fires even when KWin's bit is already clear —
        // something else cleared it and the membership must still be shed,
        // which is exactly what releaseColumnMaximized's own no-op guard
        // makes cheap.
        QTest::newRow("off/held/kwin0") << false << true << false << a(MaximizeAction::Release);
        QTest::newRow("off/held/kwin1") << false << true << true << a(MaximizeAction::Release);
        // Flag on, not yet a member: adopt. The kwin1 row is the effect-
        // restart case — the daemon still holds the state for a window this
        // effect instance has never seen, and the bit happens to survive.
        QTest::newRow("on/free/kwin0") << true << false << false << a(MaximizeAction::Apply);
        QTest::newRow("on/free/kwin1") << true << false << true << a(MaximizeAction::Apply);
        // Flag on and held but the bit went missing (KWin dropped it across a
        // screen change): re-assert rather than sit on a mirror that no
        // longer mirrors anything.
        QTest::newRow("on/held/kwin0") << true << true << false << a(MaximizeAction::Apply);
        // Steady state. THE row that keeps a maximized column from re-calling
        // maximize() for every tile on every batch.
        QTest::newRow("on/held/kwin1") << true << true << true << a(MaximizeAction::None);
    }

    void columnMaximizeTruthTable()
    {
        QFETCH(bool, flagOnWire);
        QFETCH(bool, inSet);
        QFETCH(bool, kwinMaximized);
        QFETCH(int, expectedAction);

        QCOMPARE(static_cast<int>(resolveColumnMaximizeAction(flagOnWire, inSet, kwinMaximized)), expectedAction);
    }

    // The interception's round trip cannot be raced, which is why this
    // decision carries no in-flight marker (see the contract note on
    // resolveColumnMaximizeAction). Both directions of a toggle are walked
    // here with a STALE batch landing mid-flight, asserting it is inert.
    void staleBatchDuringToggleIsInert()
    {
        // The state is THREADED through the walk rather than hand-written per
        // call, the way markerRefusesAdoptionUntilConsumed threads its marker.
        // Written as four independent calls this test asserted nothing the
        // truth table above did not already cover — every triple was a row of
        // it — so it could not fail unless the table failed too. Threading the
        // membership means a wrong TRANSITION fails here even when every
        // individual row is right, which is the property the no-marker
        // argument actually rests on.
        bool inSet = false;
        bool kwinMax = false;
        const auto step = [&inSet, &kwinMax](bool flagOnWire) {
            const MaximizeAction action = resolveColumnMaximizeAction(flagOnWire, inSet, kwinMax);
            // MODELS what the batch arm does with the answer — it does not
            // call it. kwin-effect has no linkable test target, so the arm's
            // own bookkeeping cannot be driven from here; only a wrong
            // resolver fails this test, not a wrong arm. Keep this lambda in
            // step with tiling.cpp's Apply/Release block by hand. It models
            // the UNCONDITIONAL path only, and knowingly diverges twice: the
            // real arm skips the compositor call for a fullscreen window or
            // one mid user move/resize, and releaseColumnMaximized RETAINS
            // membership on its fullscreen skip. Neither divergence affects
            // the resolver contract this test pins.
            if (action == MaximizeAction::Apply) {
                inSet = true;
                kwinMax = true;
            } else if (action == MaximizeAction::Release) {
                inSet = false;
                kwinMax = false;
            }
            return action;
        };

        // MAXIMIZING. The click is cancelled back to restore and dispatched;
        // a batch the daemon emitted before it processed the toggle still
        // says flag=false. It must not fight the click.
        QCOMPARE(static_cast<int>(step(false)), static_cast<int>(MaximizeAction::None));
        QVERIFY(!inSet);
        // The answering batch applies, and the effect takes the bit.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::Apply));
        QVERIFY(inSet);
        // A SECOND stale batch from the same flight is now the steady state.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::None));
        QVERIFY(inSet);

        // UN-MAXIMIZING, continuing from the maximized state above rather
        // than from a fresh hand-written triple.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::None));
        QVERIFY(inSet);
        QCOMPARE(static_cast<int>(step(false)), static_cast<int>(MaximizeAction::Release));
        QVERIFY(!inSet);
        // And the trailing stale batch after the release is inert too, rather
        // than re-applying and re-maximizing what the user just restored.
        QCOMPARE(static_cast<int>(step(false)), static_cast<int>(MaximizeAction::None));
        QVERIFY(!inSet);
    }

    // Counter-assert budget: matching frame never counters.
    void counterAssertNoOpOnMatchingFrame()
    {
        qint64 start = 0;
        int count = 0;
        QVERIFY(!shouldCounterAssert(start, count, 5000, /*frameDiffers=*/false));
        QCOMPARE(count, 0);
    }

    // The 4th differing event inside one rolling second is NOT countered
    // (rate limit), and the window rolls over after a second elapses.
    void counterAssertRateLimitAndRollover()
    {
        qint64 start = 0;
        int count = 0;
        // First event: nowMs=10000 is far past the zero-initialized window,
        // so the window resets and events 1-3 counter.
        QVERIFY(shouldCounterAssert(start, count, 10000, true));
        QVERIFY(shouldCounterAssert(start, count, 10100, true));
        QVERIFY(shouldCounterAssert(start, count, 10200, true));
        QCOMPARE(count, 3);
        // 4th inside the same second: refused, budget spent.
        QVERIFY(!shouldCounterAssert(start, count, 10300, true));
        QVERIFY(!shouldCounterAssert(start, count, 10900, true));
        // A second past the burst start: window rolls over, counters resume
        // — the documented indefinite 3-per-second rate limit, not an
        // exhaustible budget.
        QVERIFY(shouldCounterAssert(start, count, 11100, true));
        QCOMPARE(start, qint64(11100));
        QCOMPARE(count, 1);
    }

    // A fresh batch command re-arms the budget: the caller resets the pair
    // on batch insert (tiling.cpp writes {rect, 0, 0}), which this models.
    void counterAssertFreshBatchRearms()
    {
        qint64 start = 0;
        int count = 0;
        QVERIFY(shouldCounterAssert(start, count, 10000, true));
        QVERIFY(shouldCounterAssert(start, count, 10100, true));
        QVERIFY(shouldCounterAssert(start, count, 10200, true));
        QVERIFY(!shouldCounterAssert(start, count, 10300, true));
        // Batch insert resets the bookkeeping.
        start = 0;
        count = 0;
        QVERIFY(shouldCounterAssert(start, count, 10400, true));
    }
};

QTEST_APPLESS_MAIN(TestScrollDecisions)
#include "test_scroll_decisions.moc"

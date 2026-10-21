// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the scroll-managed window decision helpers
// (tilinghandler/scrolldecisions.h) — the windowed-fullscreen 5-way batch
// decision (with its clear-in-flight marker arm/consume contract), the
// column-maximize 3-way batch decision (with its toggle-in-flight marker and
// the two threaded stale-batch walks), the counter-assert burst budget, who it
// guards, how it puts a window back and where it re-seats one, the
// compositor-claim release table (which claim answers to which exit scope;
// the release order is static-asserted where releaseAllClaims uses it), the
// size-continuity carry-forward table, when a strip entry is a new offer,
// when a request resizes on KWin's account (resizesOnKWinsAccount), whether
// and where such a resize is put back and which requests leave the output,
// when a dropped quick tile's configure is still outstanding
// (droppedTileAckOutstanding), when a request voids a stale-ack answer, the
// engine-flip screen set, the wire screen-list filter, the
// maximize-membership and claimed-press answers, when an apply at its target
// still owes a leg (departureOwesLeg), the view-carried origin, the passive float scope, the user Monocle
// unmaximize read, the restore-rect output check, the KWin move off a
// strip, when a strip window's frame reads as its client's answer to its
// column, whether that answer is new, and which answers are reported to the
// engine. Same header-only reach as
// test_anchor_uniforms:
// kwin-effect has no linkable test target, so the pure halves are extracted
// into a header this test includes directly.

#include <tilinghandler/scrolldecisions.h>

#include <QTest>

using namespace PlasmaZones::ScrollDecisions;

namespace {

// One batch entry of a column-maximize walk: resolve, then apply the answer
// to the threaded state. MODELS what the batch arm does with the answer, it
// does not call it: kwin-effect has no linkable test target, so the arm's own
// bookkeeping cannot be driven from here, and only a wrong resolver fails a
// walk, not a wrong arm. Keep it in step with tiling.cpp's Apply/Release block
// by hand. It models the UNCONDITIONAL path only, and knowingly diverges
// twice: the real arm skips the compositor call for a fullscreen window or one
// mid user move/resize, and releaseMaximizedToEdges RETAINS membership on its
// fullscreen skip. Neither divergence affects the resolver contract the walks
// read.
MaximizeAction stepColumnMaximize(bool flagOnWire, bool& inSet, bool& kwinMax, bool armed)
{
    const MaximizeAction action = resolveMaximizeToEdgesAction(flagOnWire, inSet, kwinMax, armed);
    if (action == MaximizeAction::Apply) {
        inSet = true;
        kwinMax = true;
    } else if (action == MaximizeAction::Release) {
        inSet = false;
        kwinMax = false;
    }
    return action;
}

} // namespace

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
        // re-fullscreen the window the user just exited).
        //
        // The refusal is SINGLE-SHOT: it consumes the marker as it refuses.
        // Waiting for a flag=0 entry to consume it latched for the session,
        // because a user who re-enters windowed fullscreen before the
        // daemon's flag-off batch arrives never produces one — the flag goes
        // true again and every later batch is refused. The marker only has to
        // outlive the batches already in flight when the clear was sent.
        QTest::newRow("on/free/req0/mark0") << true << false << false << false << a(WfsAction::Adopt) << false;
        QTest::newRow("on/free/req0/mark1") << true << false << false << true << a(WfsAction::None) << true;
        QTest::newRow("on/free/req1/mark0") << true << false << true << false << a(WfsAction::Adopt) << false;
        QTest::newRow("on/free/req1/mark1") << true << false << true << true << a(WfsAction::None) << true;
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

    // The single-shot marker across the re-entry it exists for, told as a
    // story over rows the table above already pins: the marker is threaded
    // through consumeClearMarker the way the batch arm spends it. A batch
    // emitted before the daemon processed the clear is refused and spends the
    // marker in the refusal, so when the user re-enters windowed fullscreen
    // before any flag-off entry arrives, the next flagged batch adopts. The
    // latched marker this replaced waited for a flag-off entry that the
    // re-entry never produces, and refused every later batch.
    void markerRefusesOneStaleAdoptThenAdoptsTheReentry()
    {
        bool marker = true;
        const auto step = [&marker](bool flagOnWire) {
            const WfsDecision d = resolveWindowedFullscreenAction(flagOnWire, /*inSet=*/false, false, marker);
            if (d.consumeClearMarker) {
                marker = false;
            }
            return d.action;
        };
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(WfsAction::None));
        QVERIFY(!marker);
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(WfsAction::Adopt));
    }

    // The full 16-row truth table over
    // (flagOnWire, inSet, kwinMaximized, toggleInFlight).
    //
    // RETARGETED, semantics identical: the mirrored engine state is now the
    // declared maximize-to-edges flag (raw area, both axes) rather than the
    // measured column maximize; only which flag feeds flagOnWire changed,
    // so every row and its reasoning carried over verbatim.
    void columnMaximizeTruthTable_data()
    {
        QTest::addColumn<bool>("flagOnWire");
        QTest::addColumn<bool>("inSet");
        QTest::addColumn<bool>("kwinMaximized");
        QTest::addColumn<bool>("toggleInFlight");
        QTest::addColumn<int>("expectedAction");

        const auto a = [](MaximizeAction act) {
            return static_cast<int>(act);
        };
        // Flag off, not a member: nothing to do, whatever KWin's own bit
        // says. A window the USER maximized on a non-maximized column lands
        // here, and must be left alone — the batch's anti-ballooning clear
        // owns that case, not this decision.
        QTest::newRow("off/free/kwin0/unarmed") << false << false << false << false << a(MaximizeAction::None);
        QTest::newRow("off/free/kwin1/unarmed") << false << false << true << false << a(MaximizeAction::None);
        // Flag off while held: the engine dropped the maximize, so hand the
        // bit back. Release fires even when KWin's bit is already clear —
        // something else cleared it and the membership must still be shed,
        // which is exactly what releaseMaximizedToEdges's own no-op guard
        // makes cheap.
        QTest::newRow("off/held/kwin0/unarmed") << false << true << false << false << a(MaximizeAction::Release);
        QTest::newRow("off/held/kwin1/unarmed") << false << true << true << false << a(MaximizeAction::Release);
        // Flag on, not yet a member: adopt. The kwin1 row is the effect-
        // restart case — the daemon still holds the state for a window this
        // effect instance has never seen, and the bit happens to survive.
        QTest::newRow("on/free/kwin0/unarmed") << true << false << false << false << a(MaximizeAction::Apply);
        QTest::newRow("on/free/kwin1/unarmed") << true << false << true << false << a(MaximizeAction::Apply);
        // Flag on and held but the bit went missing (KWin dropped it across a
        // screen change): re-assert rather than sit on a mirror that no
        // longer mirrors anything.
        QTest::newRow("on/held/kwin0/unarmed") << true << true << false << false << a(MaximizeAction::Apply);
        // Steady state. THE row that keeps a maximized column from re-calling
        // maximize() for every tile on every batch.
        QTest::newRow("on/held/kwin1/unarmed") << true << true << true << false << a(MaximizeAction::None);

        // ARMED: a toggleMaximizeToEdges is dispatched and unanswered. Exactly
        // ONE cell moves, and it is the one a stale pre-toggle batch on the
        // restore direction lands in. Every other cell is marker-invariant,
        // which is the property that makes the marker safe to add: it cannot
        // suppress the engine's own answer.
        QTest::newRow("off/free/kwin0/armed") << false << false << false << true << a(MaximizeAction::None);
        QTest::newRow("off/free/kwin1/armed") << false << false << true << true << a(MaximizeAction::None);
        // A flag-off answer is the ENGINE speaking, so Release is never
        // suppressed. Suppressing it would strand membership for a restore
        // the engine has already granted.
        QTest::newRow("off/held/kwin0/armed") << false << true << false << true << a(MaximizeAction::Release);
        QTest::newRow("off/held/kwin1/armed") << false << true << true << true << a(MaximizeAction::Release);
        // Adopt is not suppressed either: an effect restart clears the marker
        // anyway, and refusing to adopt would drop the daemon's state.
        QTest::newRow("on/free/kwin0/armed") << true << false << false << true << a(MaximizeAction::Apply);
        QTest::newRow("on/free/kwin1/armed") << true << false << true << true << a(MaximizeAction::Apply);
        // THE CELL THE MARKER EXISTS FOR. Unarmed this is Apply, which
        // re-maximizes the window the user just restored, mid-flight, and
        // commits the stale maximized rect with it.
        QTest::newRow("on/held/kwin0/armed") << true << true << false << true << a(MaximizeAction::None);
        QTest::newRow("on/held/kwin1/armed") << true << true << true << true << a(MaximizeAction::None);
    }

    void columnMaximizeTruthTable()
    {
        QFETCH(bool, flagOnWire);
        QFETCH(bool, inSet);
        QFETCH(bool, kwinMaximized);
        QFETCH(bool, toggleInFlight);
        QFETCH(int, expectedAction);

        QCOMPARE(static_cast<int>(resolveMaximizeToEdgesAction(flagOnWire, inSet, kwinMaximized, toggleInFlight)),
                 expectedAction);
    }

    // The UNARMED steady state: no toggle in flight, so a batch is never
    // suppressed. Both directions are walked with a stale batch landing
    // mid-flight, asserting it is inert on its own terms.
    //
    // This slot deliberately models the state where the effect has ALREADY
    // observed the click's own bit flip, which is why kwinMax only ever
    // changes from inside the Apply/Release arms below. That is the harmless
    // half. The half where the user's click has cleared KWin's bit BEFORE the
    // stale batch arrives is what staleBatchOnRestoreDoesNotReMaximize covers,
    // and it is the half that needs the marker.
    //
    // Both walks are narrative over rows the truth table pins: the resolver is
    // a pure function of its four inputs, so a walk fails only where a row of
    // the table fails too. They are kept because they read each direction in
    // the order the user meets it.
    void staleBatchDuringToggleIsInert()
    {
        bool inSet = false;
        bool kwinMax = false;
        const auto step = [&inSet, &kwinMax](bool flagOnWire) {
            return stepColumnMaximize(flagOnWire, inSet, kwinMax, /*armed=*/false);
        };

        // MAXIMIZING. A batch the daemon emitted before it processed the
        // toggle still says flag=false. It must not fight the click.
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

    // THE RESTORE RACE. This is the walk the slot above structurally cannot
    // reach, and the reason the marker exists.
    //
    // The difference is WHEN KWin's bit moves. Above, kwinMax only changes
    // from inside the Apply/Release arms, so the un-maximizing leg steps
    // flag=true while kwinMax is still true and lands on the harmless
    // (1,1,1) cell. In reality the user's restore click clears KWin's bit
    // BEFORE any batch arrives — the interception no longer cancels it back —
    // so the stale batch lands on (1,1,0), which is Apply unless suppressed.
    void staleBatchOnRestoreDoesNotReMaximize()
    {
        bool inSet = true;
        bool kwinMax = true;
        bool armed = false;
        const auto step = [&inSet, &kwinMax, &armed](bool flagOnWire) {
            return stepColumnMaximize(flagOnWire, inSet, kwinMax, armed);
        };

        // The user clicks restore. KWin clears its own bit and the effect
        // writes nothing; the toggle is dispatched and now in flight.
        kwinMax = false;
        armed = true;

        // A batch the daemon emitted before it dequeued the toggle still
        // carries flag=true. Unsuppressed this is Apply, which re-maximizes
        // the window mid-flight and commits the stale maximized rect with it.
        // The table's on/held/kwin0/armed row is the assertion that fails
        // without the marker; this is that row met in sequence.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::None));
        QVERIFY(inSet);
        QVERIFY(!kwinMax);

        // The answering batch carries the engine's own verdict. Release is
        // never suppressed, so it lands even while armed.
        QCOMPARE(static_cast<int>(step(false)), static_cast<int>(MaximizeAction::Release));
        QVERIFY(!inSet);

        // The reply disarms, and a trailing stale batch is inert on its own
        // terms again.
        armed = false;
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
        // Nor does it roll the window over: the burst start stays put.
        QCOMPARE(start, qint64(0));
    }

    // The rollover boundary: a change exactly one window after the burst
    // start is still inside it, and one a millisecond later rolls it over.
    void counterAssertRolloverIsStrictlyPastTheWindow()
    {
        qint64 start = 0;
        int count = 0;
        QVERIFY(shouldCounterAssert(start, count, 10000, true));
        QVERIFY(shouldCounterAssert(start, count, 10100, true));
        QVERIFY(shouldCounterAssert(start, count, 10200, true));
        QVERIFY(!shouldCounterAssert(start, count, 11000, true));
        QCOMPARE(start, qint64(10000));
        QVERIFY(shouldCounterAssert(start, count, 11001, true));
        QCOMPARE(start, qint64(11001));
        QCOMPARE(count, 1);
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

        // The COUNT half of that reset is what actually re-arms, and the walk
        // above cannot see it: a zeroed start is already more than a window
        // behind 10400, so the rollover arm fires and zeroes the count anyway.
        // Leaving count at 3 there still passes. Spend the budget again and
        // re-arm INSIDE the rolling window, where no rollover can cover for it.
        QVERIFY(shouldCounterAssert(start, count, 10410, true));
        QVERIFY(shouldCounterAssert(start, count, 10420, true));
        QVERIFY(!shouldCounterAssert(start, count, 10430, true));
        count = 0;
        QVERIFY(shouldCounterAssert(start, count, 10450, true));
        QCOMPARE(start, qint64(10400));
        QCOMPARE(count, 1);
    }

    // Who the counter-assert guards. Every row asserted, so the Wayland
    // exclusion has to be argued for per population rather than assumed.
    void counterAssertMovableTable_data()
    {
        QTest::addColumn<bool>("inTransition");
        QTest::addColumn<bool>("wayland");
        QTest::addColumn<bool>("member");
        QTest::addColumn<bool>("outOfView");
        QTest::addColumn<bool>("movable");
        QTest::newRow("x11 on screen") << false << false << false << false << true;
        QTest::newRow("x11 parked") << false << false << false << true << true;
        QTest::newRow("wayland on screen (cannot move itself)") << false << true << false << false << false;
        QTest::newRow("wayland parked (6.8 clamp)") << false << true << false << true << true;
        QTest::newRow("wayland maximized member") << false << true << true << false << true;
        QTest::newRow("x11 mid maximize transition") << true << false << false << false << false;
        QTest::newRow("wayland parked mid maximize transition") << true << true << false << true << false;
        QTest::newRow("wayland member mid maximize transition") << true << true << true << false << false;
    }

    void counterAssertMovableTable()
    {
        QFETCH(bool, inTransition);
        QFETCH(bool, wayland);
        QFETCH(bool, member);
        QFETCH(bool, outOfView);
        QFETCH(bool, movable);
        QCOMPARE(counterAssertMovable(inTransition, wayland, member, outOfView), movable);
    }

    // How the counter puts a window back: by position (a move that keeps the
    // leaving leg) only out of view, and outside a plain Wayland column only
    // while the size still matches, so a mover that resized gets the size back.
    void counterAssertsByPositionTable_data()
    {
        QTest::addColumn<bool>("outOfView");
        QTest::addColumn<bool>("wayland");
        QTest::addColumn<bool>("member");
        QTest::addColumn<bool>("sizeMatches");
        QTest::addColumn<bool>("byPosition");
        QTest::newRow("on screen, same size") << false << false << false << true << false;
        QTest::newRow("wayland on screen") << false << true << false << false << false;
        QTest::newRow("wayland park, client holds another size") << true << true << false << false << true;
        QTest::newRow("wayland park, same size") << true << true << false << true << true;
        QTest::newRow("member park, same size") << true << true << true << true << true;
        QTest::newRow("member park, KWin resized it") << true << true << true << false << false;
        QTest::newRow("x11 park, same size") << true << false << false << true << true;
        QTest::newRow("x11 park, client resized itself") << true << false << false << false << false;
    }

    void counterAssertsByPositionTable()
    {
        QFETCH(bool, outOfView);
        QFETCH(bool, wayland);
        QFETCH(bool, member);
        QFETCH(bool, sizeMatches);
        QFETCH(bool, byPosition);
        QCOMPARE(counterAssertsByPosition(outOfView, wayland, member, sizeMatches), byPosition);
    }

    // The re-seat point: centred in the column, the shift clamped at zero.
    void reseatOrigin_centresASmallerFrame()
    {
        QCOMPARE(reseatOrigin(QRect(100, 816, 600, 784), QSize(400, 600)), QPoint(200, 908));
    }

    void reseatOrigin_fullSizeIsTheColumnOrigin()
    {
        QCOMPARE(reseatOrigin(QRect(100, 816, 600, 784), QSize(600, 784)), QPoint(100, 816));
    }

    void reseatOrigin_largerFrameStaysAtTheOrigin()
    {
        QCOMPARE(reseatOrigin(QRect(100, 816, 600, 784), QSize(700, 900)), QPoint(100, 816));
    }

    void reseatOrigin_oddGapRoundsDown()
    {
        QCOMPARE(reseatOrigin(QRect(0, 0, 601, 784), QSize(600, 781)), QPoint(0, 1));
    }

    // ── Compositor-state claims ────────────────────────────────────────────
    //
    // The release table is the whole point of the claim vocabulary: a missing
    // release is an ABSENCE in the old scattered form, so it compiled, tested
    // and reviewed clean while stranding compositor state. Here every cell is
    // asserted, so a blank has to be argued for rather than merely forgotten.

    void claimReleaseTable_data()
    {
        QTest::addColumn<int>("claim");
        QTest::addColumn<int>("scope");
        QTest::addColumn<bool>("releases");

        const auto c = [](Claim k) {
            return static_cast<int>(k);
        };
        const auto s = [](ClaimScope k) {
            return static_cast<int>(k);
        };

        // StripExit — the defining case: the window is leaving and no later
        // batch will carry it, so every claim must pay up.
        QTest::newRow("stripExit/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::StripExit) << true;
        QTest::newRow("stripExit/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::StripExit) << true;
        QTest::newRow("stripExit/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::StripExit) << true;

        // The untrack funnel for a live window releases all three: no batch
        // follows it, and the monocle arm takes no membership over a window
        // already at MaximizeFull, so a scrubbed monocle claim left KWin's
        // maximize with nobody to hand it back.
        QTest::newRow("untrack/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::UntrackFunnel) << true;
        QTest::newRow("untrack/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::UntrackFunnel) << true;
        QTest::newRow("untrack/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::UntrackFunnel) << true;

        // Two monocle blanks follow, and both are decided. A closing window
        // has nothing to restore, so its monocle membership goes with the
        // scrub. PassiveFloat's blank: re-driving a maximize restore from a
        // passive float signal has not been shown safe against the monocle
        // batch that owns that membership.
        QTest::newRow("close/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::Close) << false;
        QTest::newRow("close/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::Close) << true;
        QTest::newRow("close/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::Close) << true;

        QTest::newRow("passiveFloat/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::PassiveFloat) << false;
        QTest::newRow("passiveFloat/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::PassiveFloat) << true;
        QTest::newRow("passiveFloat/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::PassiveFloat) << true;

        QTest::newRow("modeFlip/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::ModeFlip) << true;
        QTest::newRow("modeFlip/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::ModeFlip) << true;
        QTest::newRow("modeFlip/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::ModeFlip) << true;

        // FullscreenExit pays the maximize claims retained through the hold.
        // The windowed-fullscreen blank keeps the membership a member leaving
        // it with the daemon gone holds for the deferred reconcile: the
        // hand-written pair these exit arms used to call released exactly the
        // two maximize claims, so routing them through the funnel changed
        // nothing a window sees.
        QTest::newRow("fsExit/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::FullscreenExit) << true;
        QTest::newRow("fsExit/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::FullscreenExit) << false;
        QTest::newRow("fsExit/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::FullscreenExit) << true;

        QTest::newRow("teardown/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::Teardown) << true;
        QTest::newRow("teardown/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::Teardown) << true;
        QTest::newRow("teardown/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::Teardown) << true;

        // An evacuee keeps KWin's state: no claim releases (the ledgers are scrubbed bare).
        QTest::newRow("evacuation/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::Evacuation) << false;
        QTest::newRow("evacuation/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::Evacuation) << false;
        QTest::newRow("evacuation/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::Evacuation) << false;

        // A snap placement handed every claim back already, at its own rect.
        QTest::newRow("snapPlacement/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::SnapPlacement) << false;
        QTest::newRow("snapPlacement/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::SnapPlacement) << false;
        QTest::newRow("snapPlacement/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::SnapPlacement) << false;

        // A live window that left the context in view hands every claim back,
        // the monocle maximize included (F675).
        QTest::newRow("liveDeparture/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::LiveDeparture) << true;
        QTest::newRow("liveDeparture/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::LiveDeparture) << true;
        QTest::newRow("liveDeparture/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::LiveDeparture) << true;
    }

    void claimReleaseTable()
    {
        QFETCH(int, claim);
        QFETCH(int, scope);
        QFETCH(bool, releases);
        QCOMPARE(claimReleasesOn(static_cast<Claim>(claim), static_cast<ClaimScope>(scope)), releases);
    }

    void sizeContinuityCarryForwardTable_data()
    {
        QTest::addColumn<bool>("declaredRect");
        QTest::addColumn<bool>("columnAnswered");
        QTest::addColumn<QSize>("committed");
        QTest::addColumn<QSize>("column");
        QTest::addColumn<bool>("carries");

        const QSize column(1610, 879);

        // The ordinary case the mechanism exists for: a client that answered
        // this column with a slightly different size keeps it, as a pure move.
        QTest::newRow("client answered smaller") << false << true << QSize(1608, 877) << column << true;
        // No entry yet, so this batch is the one that offers the real column.
        QTest::newRow("column not yet answered") << false << false << QSize(1608, 877) << column << false;
        // Nothing to correct.
        QTest::newRow("client took the column") << false << true << column << column << false;
        // A degenerate mid-unmap commit must not centre the window by the
        // whole column.
        QTest::newRow("empty committed size") << false << true << QSize() << column << false;

        // THE REGRESSION. A maximize-to-edges column is declared state: its
        // rect is the raw work area, and carrying a client's own preference
        // forward commits the window at that preference, centred — which is
        // "maximize is not full width" exactly as reported.
        QTest::newRow("declared rect, client answered smaller")
            << true << true << QSize(833, 790) << QSize(1670, 939) << false;
        QTest::newRow("declared rect, client took it") << true << true << QSize(1670, 939) << QSize(1670, 939) << false;

        // A frame larger than the column is never carried, for two reasons.
        // The paired hazard on the way back out: on the un-maximize batch the
        // column is restored while the client still holds the raw work area,
        // and a surviving pre-maximize entry matches the restored size. The
        // centring maths clamps at zero, so carrying this forward would commit
        // the maximized size at the restored column's origin. And a client
        // that genuinely will not fit the column answers larger, which is the
        // engine's to settle through the committed-size report: carrying it
        // would freeze an overhang the engine has not been told about.
        QTest::newRow("stale oversized frame, both axes") << false << true << QSize(1670, 939) << column << false;
        QTest::newRow("stale oversized frame, width only") << false << true << QSize(1670, 800) << column << false;
        QTest::newRow("stale oversized frame, height only") << false << true << QSize(1400, 939) << column << false;

        // The equality edges of that same refusal. A frame matching the column
        // on exactly one axis IS an answer to this column and must still
        // carry, so these are the rows that fail if either bound in
        // mayCarryCommittedSize is tightened from <= to <. Without them both
        // mutations pass the whole table: every other carrying row is strictly
        // smaller on both axes.
        QTest::newRow("equal width, shorter height") << false << true << QSize(1610, 877) << column << true;
        QTest::newRow("equal height, narrower width") << false << true << QSize(1608, 879) << column << true;
    }

    void sizeContinuityCarryForwardTable()
    {
        QFETCH(bool, declaredRect);
        QFETCH(bool, columnAnswered);
        QFETCH(QSize, committed);
        QFETCH(QSize, column);
        QFETCH(bool, carries);
        QCOMPARE(mayCarryCommittedSize(declaredRect, columnAnswered, committed, column), carries);
    }

    // An entry is a new offer when it sends the client a size, whatever the
    // rect does: the move-resize split, one row each way.
    void stripEntrySendsSize_onlyWhenTheSizeDiffers()
    {
        QVERIFY(stripEntrySendsSize(QSize(2880, 2052), QSize(1908, 2052)));
        QVERIFY(!stripEntrySendsSize(QSize(1908, 2052), QSize(1908, 2052)));
    }

    // A request outside the effect's applies resizes on KWin's account only
    // when its size is neither the frame's nor the offered column's (F141).
    // The live shape: KWin's Expand Window takes a 788 px tile to 1592.
    void resizesOnKWinsAccount_onlyANewSize()
    {
        const QSize column(788, 884);
        const auto at = [](qreal x, const QSizeF& size) {
            return QRectF(QPointF(x, 8), size);
        };
        const QRectF frame = at(8, column);
        QVERIFY(resizesOnKWinsAccount(at(8, QSizeF(1592, 884)), frame, /*offered=*/column));
        // Shrink Window, and a script setting a smaller frame.
        QVERIFY(resizesOnKWinsAccount(at(8, QSizeF(600, 884)), frame, column));
        // A size-keeping request: the keep-visible clamp or a script's move.
        QVERIFY(!resizesOnKWinsAccount(at(400, column), frame, column));
        // A move while the strip's own resize to the column is still in flight:
        // KWin's move keeps the requested size, which is the column, not the
        // frame the client has not answered yet.
        QVERIFY(!resizesOnKWinsAccount(at(400, column), at(8, QSizeF(1000, 884)), column));
        // A move after the client answered wider: at rest KWin's move-resize
        // size is the committed frame, so the move keeps the frame's size.
        QVERIFY(!resizesOnKWinsAccount(at(400, QSizeF(1000, 884)), at(8, QSizeF(1000, 884)), column));
        // A move that keeps a fractional size, at a fractional scale. Rounded
        // through QRectF::toRect the two rects come out 788 and 789 wide,
        // because that rounding depends on where the rect sits, and the move
        // read as a resize. The offer is unrelated, so only the frame
        // comparison can refuse it.
        const QSizeF fractional(788.6, 884);
        QVERIFY(!resizesOnKWinsAccount(at(0.6, fractional), at(0.4, fractional), QSize(700, 884)));
    }

    // The quick-tile corrector's stale ack (F123). KWin's tile sends a
    // configure for the tile's size; dropping the tile and putting the column
    // back at the size the client still holds sends none, so a tile configure
    // already sent is still the newest the client has and its ack would land
    // the tile's frame, read as the answer to the column. One KWin had only
    // scheduled goes out carrying the column instead; the predicate cannot tell
    // the two apart, and the answer it arms then waits until something voids
    // it (requestVoidsStaleAck below).
    void droppedTileAckOutstanding_onlyWhenNothingSupersedesTheTile()
    {
        const QSize column(788, 884);
        const QSize tile(960, 1080);
        // The tile was requested and the client has not acked it: the column
        // goes back at the client's own size, with no configure.
        QVERIFY(droppedTileAckOutstanding(/*requestedAtDrop=*/tile, /*committedAtDrop=*/column,
                                          /*commandedAfter=*/column, /*committedAfter=*/column));
        // The client acked the tile long ago and sits at its size: the column
        // goes out in a configure of its own, which nothing stale precedes.
        QVERIFY(!droppedTileAckOutstanding(tile, tile, column, tile));
        // The column differs from what the client holds, so KWin configures
        // it and that configure supersedes the tile's.
        QVERIFY(!droppedTileAckOutstanding(tile, QSize(700, 884), column, QSize(700, 884)));
        // Nothing in flight at the drop and nothing to change.
        QVERIFY(!droppedTileAckOutstanding(column, column, column, column));
    }

    // A request outside the effect's applies for another size makes KWin
    // send a configure of its own, which supersedes a stale one, so the answer
    // armed for that stale one is void (CR2-22). The live shape: an answer the
    // quick-tile corrector armed at the 788 px column outlived the tile's
    // configure, and KWin's Expand Window to 1592 later read as the tile's ack
    // and was re-issued back to the column.
    void requestVoidsStaleAck_onlyForAnotherSize()
    {
        const QSize target(788, 884);
        QVERIFY(requestVoidsStaleAck(QSizeF(1592, 884), target));
        QVERIFY(requestVoidsStaleAck(QSizeF(600, 884), target));
        // The other axis alone (a script setting a shorter frame).
        QVERIFY(requestVoidsStaleAck(QSizeF(788, 700), target));
        // A request that keeps the size (a move, the keep-visible clamp) sends
        // no configure, so the stale one is still outstanding.
        QVERIFY(!requestVoidsStaleAck(QSizeF(788, 884), target));
        // Fractional-scale residue is not a new size; two pixels are.
        QVERIFY(!requestVoidsStaleAck(QSizeF(788.6, 884), target));
        QVERIFY(!requestVoidsStaleAck(QSizeF(789, 883), target));
        QVERIFY(requestVoidsStaleAck(QSizeF(790, 884), target));
        QVERIFY(requestVoidsStaleAck(QSizeF(788, 882), target));
    }

    // Whether a resize KWin requested for a strip window on its own account is
    // put back (CR2-kwin-resize). Each refusing row clears exactly one gate
    // from the all-open baseline, so a gate dropped from the predicate fails
    // its own row.
    void kwinResizePutBackTable_data()
    {
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("coordinatesHold");
        QTest::addColumn<bool>("kwinStateAtRest");
        QTest::addColumn<bool>("userGesture");
        QTest::addColumn<bool>("leavesOutput");
        QTest::addColumn<bool>("putsBack");

        // KWin's Expand or Shrink Window, or a script setting the frame.
        QTest::newRow("every gate open") << true << true << true << false << false << true;
        // Floating, untracked, or under a user move or resize.
        QTest::newRow("not scroll-managed") << false << true << true << false << false << false;
        // The rect to put back is in coordinates the output has left (F815).
        QTest::newRow("coordinates stale") << true << false << true << false << false << false;
        // KWin's maximize, fullscreen or quick tile, which have correctors of
        // their own.
        QTest::newRow("KWin state requested") << true << true << false << false << false << false;
        // A drag the tracker still owns, or the resize hold.
        QTest::newRow("user gesture") << true << true << true << true << false << false;
        // KWin's Move to Screen onto a work area smaller than the column, which
        // resizes on the way: put back, it would never reach the other output.
        QTest::newRow("moves to another output") << true << true << true << false << true << false;
    }

    void kwinResizePutBackTable()
    {
        QFETCH(bool, managed);
        QFETCH(bool, coordinatesHold);
        QFETCH(bool, kwinStateAtRest);
        QFETCH(bool, userGesture);
        QFETCH(bool, leavesOutput);
        QFETCH(bool, putsBack);
        KWinResizePutBackGates gates;
        gates.managed = managed;
        gates.coordinatesHold = coordinatesHold;
        gates.kwinStateAtRest = kwinStateAtRest;
        gates.userGesture = userGesture;
        gates.leavesOutput = leavesOutput;
        QCOMPARE(putsBackKWinResize(gates), putsBack);
    }

    // Which requests leave the output, read through the output-change arm's
    // own test (isKWinMoveOffStrip) with the requested rect as the frame. The
    // live shape: a 1908 px column on a 3840 px output sent to a 1280 px one,
    // where Window::sendToOutput's keepInArea shrinks it to the new area.
    void kwinResizePutBack_leavesOutputOnlyForAMoveOff()
    {
        const QRectF stripOutput(0, 0, 3840, 2160);
        // Move to Screen onto the smaller output to the right: handed over.
        QVERIFY(isKWinMoveOffStrip(QRectF(3848, 8, 1264, 1004), stripOutput, /*markerArmed=*/false,
                                   /*parked=*/false, /*userMove=*/false));
        // Expand Window of a column straddling the output's right edge: its
        // centre crosses onto the neighbour, but it still overlaps the strip's
        // output, so it is put back.
        QVERIFY(!isKWinMoveOffStrip(QRectF(3000, 8, 1592, 2052), stripOutput, false, false, false));
        // Expand Window of a parked column: off every output by design, and
        // its nearest output says nothing about a move, so it is put back.
        QVERIFY(!isKWinMoveOffStrip(QRectF(8, 2200, 2400, 2052), stripOutput, false, /*parked=*/true, false));
    }

    // Where the put-back lands. The strip's commanded rect wins where the
    // counter-assert keeps one; else the column while its answer is owed, and
    // the settled frame once it was read.
    void kwinResizePutBackRect_followsTheStripsRect()
    {
        const QRect column(8, 8, 1908, 2052);
        const QRectF carried(108, 8, 1708, 2052);
        const QRect park(8, 5000, 1908, 2052);
        // A column placed outside its output: the rect the strip committed.
        QCOMPARE(kwinResizePutBackRect(park, ColumnAnswer::Read, column, carried), QRectF(park));
        QCOMPARE(kwinResizePutBackRect(park, ColumnAnswer::Owed, column, carried), QRectF(park));
        // Owed: the frame is the one the strip's resize is replacing.
        QCOMPARE(kwinResizePutBackRect(QRect(), ColumnAnswer::Owed, column, QRectF(8, 8, 1000, 2052)), QRectF(column));
        // Read: the size the client settled on, which the strip carries
        // centred in the column.
        QCOMPARE(kwinResizePutBackRect(QRect(), ColumnAnswer::Read, column, carried), carried);
        QCOMPARE(kwinResizePutBackRect(QRect(), ColumnAnswer::Read, column, QRectF(column)), QRectF(column));
        // A fractional frame goes back exact. Through QRectF::toRect this one
        // rounds to 789 wide, a pixel off the client's own size, and the
        // put-back would itself send the client a resize.
        const QRectF fractional(8.4, 8, 788.4, 2052);
        QCOMPARE(fractional.toRect().width(), 789);
        QCOMPARE(kwinResizePutBackRect(QRect(), ColumnAnswer::Read, column, fractional), fractional);
        // Nothing to put back: a Foreign answer, or an empty frame.
        QVERIFY(kwinResizePutBackRect(QRect(), ColumnAnswer::Foreign, column, QRectF(column)).isEmpty());
        QVERIFY(kwinResizePutBackRect(QRect(), ColumnAnswer::Read, column, QRectF(100, 8, 0, 2052)).isEmpty());
    }

    // The engine forces a batch with every rect unchanged when minimum sizes
    // are turned on (F50), so an answer it refused to learn while they were off
    // can be heard. Walked over the effect's pure pieces for the three shapes a
    // client can be in when that batch lands, each with the same column offered
    // again and the client at rest (KWin's move-resize size is its frame).
    void aForcedBatchReOffersTheColumnToAClientThatDidNotTakeIt()
    {
        const QSize column(1908, 2052);
        // Wider along the strip: never carried, so the column goes out again,
        // as a size, and the settled answer is owed and reported.
        const QSize wider(2880, 2052);
        QVERIFY(!mayCarryCommittedSize(false, /*columnAnswered=*/true, wider, column));
        QVERIFY(stripEntrySendsSize(/*commanded=*/wider, /*delivered=*/column));
        QVERIFY(isNewColumnAnswer(ColumnAnswer::Owed, wider, wider));
        QVERIFY(shouldReportCommittedSize(column, wider, /*vertical=*/false));
        // Longer across it: also never carried, so also re-offered.
        const QSize taller(1908, 2400);
        QVERIFY(!mayCarryCommittedSize(false, true, taller, column));
        QVERIFY(stripEntrySendsSize(taller, column));
        // Narrower: carried, so nothing is offered and nothing re-read, which
        // loses nothing, because a narrower answer never sets a floor.
        const QSize narrower(1500, 2052);
        QVERIFY(mayCarryCommittedSize(false, true, narrower, column));
        QVERIFY(!stripEntrySendsSize(narrower, narrower));
        // A client that took the column has nothing to answer.
        QVERIFY(!stripEntrySendsSize(column, column));
        QVERIFY(!shouldReportCommittedSize(column, column, false));
    }

    // The engine flip inside the tiling union (F231, F286, F1004): only a
    // screen that stays in the union while it changes engine is re-announced.
    void engineFlip_autotileToScrolling()
    {
        const QSet<QString> s{QStringLiteral("S")};
        QCOMPARE(engineFlipScreens({}, s, s, false, {}), s);
    }
    void engineFlip_scrollingToAutotile()
    {
        const QSet<QString> s{QStringLiteral("S")};
        QCOMPARE(engineFlipScreens(s, {}, s, false, s), s);
    }
    void engineFlip_scrollingToSnapping()
    {
        const QSet<QString> s{QStringLiteral("S")};
        QVERIFY(engineFlipScreens(s, {}, s, false, {}).isEmpty());
    }
    void engineFlip_contextSwitchFlipsNothing()
    {
        const QSet<QString> s{QStringLiteral("S")};
        QVERIFY(engineFlipScreens(s, {}, s, true, s).isEmpty());
        QVERIFY(engineFlipScreens({}, s, s, true, {}).isEmpty());
    }
    void engineFlip_snappingToScrollingIsNotManaged()
    {
        const QSet<QString> s{QStringLiteral("S")};
        QVERIFY(engineFlipScreens({}, s, {}, false, {}).isEmpty());
    }
    // A screen that stays scrolling flips nothing, while another enters: only
    // the entering one is re-announced. Without the set differences every
    // stable scrolling screen would be re-announced on every change.
    void engineFlip_stableScreenIsNotReannounced()
    {
        const QString stable = QStringLiteral("S");
        const QString entering = QStringLiteral("T");
        const QSet<QString> managed{stable, entering};
        QCOMPARE(engineFlipScreens({stable}, managed, managed, false, {}), QSet<QString>{entering});
        QVERIFY(engineFlipScreens({stable}, {stable}, {stable}, false, {}).isEmpty());
    }

    // The scrolling-set boundary (F359): an empty wire id is dropped, and the
    // rest collapse to a set.
    void nonEmptyIdSet_dropsEmpty()
    {
        const QStringList ids{QStringLiteral("A"), QString(), QStringLiteral("A"), QStringLiteral("B")};
        QCOMPARE(nonEmptyIdSet(ids), (QSet<QString>{QStringLiteral("A"), QStringLiteral("B")}));
    }
    void nonEmptyIdSet_allEmpty()
    {
        QVERIFY(nonEmptyIdSet(QStringList{QString()}).isEmpty());
    }

    // A maximize Apply arm takes the claim only for a window KWin will
    // maximize, or one already held at MaximizeFull (F680). The arms used to
    // take it unconditionally.
    void takesMaximizeMembership_refusedNotMaximized_false()
    {
        QVERIFY(!takesMaximizeMembership(/*maximizable=*/false, /*alreadyFull=*/false));
    }
    void takesMaximizeMembership_alreadyFull_true()
    {
        QVERIFY(takesMaximizeMembership(/*maximizable=*/false, /*alreadyFull=*/true));
    }
    void takesMaximizeMembership_maximizable_true()
    {
        QVERIFY(takesMaximizeMembership(/*maximizable=*/true, /*alreadyFull=*/false));
    }
    // Both: a maximizable window already at MaximizeFull (the adopt) still
    // takes it, which an exclusive-or would refuse.
    void takesMaximizeMembership_maximizableAndAlreadyFull_true()
    {
        QVERIFY(takesMaximizeMembership(/*maximizable=*/true, /*alreadyFull=*/true));
    }

    // The answer to a claimed titlebar maximize finds KWin already maximized
    // and writes no bit; it still anchors at the press (F989).
    void answersClaimedPress_maximizeAnswer()
    {
        QVERIFY(answersClaimedPress(MaximizeAction::Apply, /*kwinMaximized=*/true, /*markerLive=*/true));
    }
    // The answer to a claimed restore finds KWin already restored, and rides placeOut.
    void answersClaimedPress_restoreAnswer()
    {
        QVERIFY(answersClaimedPress(MaximizeAction::Release, /*kwinMaximized=*/false, /*markerLive=*/true));
    }
    // An Apply that writes the bit itself (the shortcut path) or a Release that
    // writes it back is not the answer to a claimed press, and leaves the marker.
    void answersClaimedPress_armThatWritesIsNot()
    {
        QVERIFY(!answersClaimedPress(MaximizeAction::Apply, /*kwinMaximized=*/false, /*markerLive=*/true));
        QVERIFY(!answersClaimedPress(MaximizeAction::Release, /*kwinMaximized=*/true, /*markerLive=*/true));
        QVERIFY(!answersClaimedPress(MaximizeAction::None, /*kwinMaximized=*/true, /*markerLive=*/true));
        // None with KWin restored: no arm at all answers, whichever way the
        // bit sits.
        QVERIFY(!answersClaimedPress(MaximizeAction::None, /*kwinMaximized=*/false, /*markerLive=*/true));
    }
    // No live marker: the effect-restart adopt (Apply over a held bit) is no
    // press, and neither is a Release that finds KWin restored.
    void answersClaimedPress_noMarkerIsNot()
    {
        QVERIFY(!answersClaimedPress(MaximizeAction::Apply, /*kwinMaximized=*/true, /*markerLive=*/false));
        QVERIFY(!answersClaimedPress(MaximizeAction::Release, /*kwinMaximized=*/false, /*markerLive=*/false));
    }
    // The answer to a claimed press owes its leg even when KWin's flip already
    // commanded the batch's rect (F989): maximize from the column, restore from
    // the maximize area. So does a batch whose quick-tile drop KWin's untile
    // answered by moving the window at once to the column itself: the leg runs
    // from the tile (CR2-24).
    void departureOwesLeg_atTargetStillAnimates()
    {
        QVERIFY(departureOwesLeg(true, QRectF(8, 8, 500, 752), QRect(0, 0, 1024, 768)));
        QVERIFY(departureOwesLeg(true, QRectF(0, 0, 1024, 768), QRect(8, 8, 500, 752)));
        QVERIFY(departureOwesLeg(true, /*tile=*/QRectF(0, 0, 960, 1080), /*column=*/QRect(8, 8, 788, 1064)));
    }
    // Any other apply at its target stays a no-op, and so does one with no
    // departure of its own.
    void departureOwesLeg_otherwiseNoOp()
    {
        QVERIFY(!departureOwesLeg(false, QRectF(8, 8, 500, 752), QRect(0, 0, 1024, 768)));
        QVERIFY(!departureOwesLeg(true, QRectF(0, 0, 1024, 768), QRect(0, 0, 1024, 768)));
        QVERIFY(!departureOwesLeg(true, QRectF(), QRect(0, 0, 1024, 768)));
        // A degenerate origin with a position but no width is no departure.
        QVERIFY(!departureOwesLeg(true, QRectF(8, 8, 0, 752), QRect(0, 0, 1024, 768)));
    }
    // A view-carried leg starts a view delta behind where it should first
    // paint, along the strip's axis only: the X11 maximize of the second column
    // (the view slides 508) departs from the column the press left, 516,8,
    // and paints there on its first frame (F989).
    void viewCarriedOrigin_horizontalBacksOutX()
    {
        QCOMPARE(viewCarriedOrigin(QRectF(516, 8, 500, 752), 508, /*vertical=*/false), QRect(8, 8, 500, 752));
    }
    void viewCarriedOrigin_verticalBacksOutY()
    {
        QCOMPARE(viewCarriedOrigin(QRectF(8, 516, 752, 500), 508, /*vertical=*/true), QRect(8, 8, 752, 500));
    }
    // Rounded, so sub-pixel residue makes no leg of a pure scroll.
    void viewCarriedOrigin_roundsResidue()
    {
        QCOMPARE(viewCarriedOrigin(QRectF(516.4, 7.6, 499.6, 752.4), 0, false), QRect(516, 8, 500, 752));
    }
    // The passive float channel on a scrolling screen releases every claim,
    // monocle included: no strip batch owns a monocle membership (F723).
    void passiveFloatScope_scrollingReleasesMonocle()
    {
        const ClaimScope scope = passiveFloatScope(/*onScrollingScreen=*/true);
        QCOMPARE(static_cast<int>(scope), static_cast<int>(ClaimScope::StripExit));
        QVERIFY(claimReleasesOn(Claim::MonocleMaximize, scope));
        QVERIFY(claimReleasesOn(Claim::WindowedFullscreen, scope));
        QVERIFY(claimReleasesOn(Claim::MaximizedToEdges, scope));
    }
    // Off a strip the autotile monocle batch owns the membership, so the
    // passive float leaves it (the PassiveFloat blank stays).
    void passiveFloatScope_autotileKeepsMonocle()
    {
        const ClaimScope scope = passiveFloatScope(/*onScrollingScreen=*/false);
        QCOMPARE(static_cast<int>(scope), static_cast<int>(ClaimScope::PassiveFloat));
        QVERIFY(!claimReleasesOn(Claim::MonocleMaximize, scope));
        QVERIFY(claimReleasesOn(Claim::WindowedFullscreen, scope));
        QVERIFY(claimReleasesOn(Claim::MaximizedToEdges, scope));
    }

    // A Restore commit while KWin still holds the Full request is the late ack
    // of a superseded configure, not the user (F1013).
    void userMonocleUnmaximize_supersededRestoreAck_false()
    {
        QVERIFY(!isUserMonocleUnmaximize(/*committedH=*/false, /*committedV=*/false, /*requestedFull=*/true));
    }
    void userMonocleUnmaximize_restoreRequested_true()
    {
        QVERIFY(isUserMonocleUnmaximize(/*committedH=*/false, /*committedV=*/false, /*requestedFull=*/false));
    }
    // A commit with either axis still maximized is no un-maximize at all.
    void userMonocleUnmaximize_horizontalStillMaximized_false()
    {
        QVERIFY(!isUserMonocleUnmaximize(/*committedH=*/true, /*committedV=*/false, /*requestedFull=*/false));
    }
    void userMonocleUnmaximize_verticalStillMaximized_false()
    {
        QVERIFY(!isUserMonocleUnmaximize(/*committedH=*/false, /*committedV=*/true, /*requestedFull=*/false));
    }

    // A monocle member's restore rect must lead back onto its stack's output
    // (F1011): one left on the strip's monitor is re-seated.
    void restoreRectOnOutput_onTheOutput_true()
    {
        QVERIFY(restoreRectOnOutput(QRectF(1928, 8, 500, 752), QRectF(1920, 0, 1920, 1080)));
    }
    void restoreRectOnOutput_otherOutput_false()
    {
        QVERIFY(!restoreRectOnOutput(QRectF(8, 8, 500, 752), QRectF(1920, 0, 1920, 1080)));
    }
    void restoreRectOnOutput_invalid_false()
    {
        QVERIFY(!restoreRectOnOutput(QRectF(), QRectF(0, 0, 1920, 1080)));
    }
    // Straddling the edge with its centre on the other output leads back
    // there, though the rect still overlaps this one.
    void restoreRectOnOutput_straddlingCentreOff_false()
    {
        QVERIFY(!restoreRectOnOutput(QRectF(1700, 8, 600, 752), QRectF(0, 0, 1920, 1080)));
    }

    // KWin's own Move to Screen of a strip tile is a real move; the engine's
    // handoffs, a parked column, a drag and a straddling column are not (F738).
    void kwinMoveOffStrip_frameOnAnotherOutput_true()
    {
        QVERIFY(isKWinMoveOffStrip(QRectF(1928, 8, 500, 752), QRectF(0, 0, 1920, 1080), /*markerArmed=*/false,
                                   /*parked=*/false, /*userMove=*/false));
    }
    void kwinMoveOffStrip_engineHandoff_false()
    {
        QVERIFY(!isKWinMoveOffStrip(QRectF(1928, 8, 500, 752), QRectF(0, 0, 1920, 1080), /*markerArmed=*/true,
                                    /*parked=*/false, /*userMove=*/false));
    }
    void kwinMoveOffStrip_parkedColumn_false()
    {
        QVERIFY(!isKWinMoveOffStrip(QRectF(1928, 8, 500, 752), QRectF(0, 0, 1920, 1080), /*markerArmed=*/false,
                                    /*parked=*/true, /*userMove=*/false));
    }
    void kwinMoveOffStrip_drag_false()
    {
        QVERIFY(!isKWinMoveOffStrip(QRectF(1928, 8, 500, 752), QRectF(0, 0, 1920, 1080), /*markerArmed=*/false,
                                    /*parked=*/false, /*userMove=*/true));
    }
    void kwinMoveOffStrip_straddlingColumn_false()
    {
        // Centre on the neighbour, but still overlapping the commanded output.
        QVERIFY(!isKWinMoveOffStrip(QRectF(1700, 8, 600, 752), QRectF(0, 0, 1920, 1080), /*markerArmed=*/false,
                                    /*parked=*/false, /*userMove=*/false));
    }
    void kwinMoveOffStrip_noCommandedRect_false()
    {
        QVERIFY(!isKWinMoveOffStrip(QRectF(1928, 8, 500, 752), QRectF(), /*markerArmed=*/false, /*parked=*/false,
                                    /*userMove=*/false));
    }
    // A degenerate frame (a mid-unmap commit) intersects nothing, which is no
    // evidence of a move.
    void kwinMoveOffStrip_noFrame_false()
    {
        QVERIFY(!isKWinMoveOffStrip(QRectF(), QRectF(0, 0, 1920, 1080), /*markerArmed=*/false, /*parked=*/false,
                                    /*userMove=*/false));
    }

    // When a strip window's frame reads as its client's answer to its column:
    // the ONE predicate the report and the offered-column centring share.
    // Each refusing row clears exactly one gate from the all-open baseline,
    // so a gate dropped from the predicate fails its own row.
    void columnAnswerReadabilityTable_data()
    {
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("coordinatesHold");
        QTest::addColumn<bool>("kwinStateAtRest");
        QTest::addColumn<bool>("resizeHeld");
        QTest::addColumn<bool>("settled");
        QTest::addColumn<int>("answer");
        QTest::addColumn<QRect>("live");
        QTest::addColumn<bool>("reads");

        const int owed = static_cast<int>(ColumnAnswer::Owed);
        const int read = static_cast<int>(ColumnAnswer::Read);
        const int foreign = static_cast<int>(ColumnAnswer::Foreign);
        // The column is (8, 8, 1908, 2052); the client answered wider.
        const QRect wider(8, 8, 2880, 2052);

        QTest::newRow("every gate open, answer owed") << true << true << true << false << true << owed << wider << true;
        // A Read answer is still readable: a size the client settles on by
        // itself later is the engine's to hear (isNewColumnAnswer decides).
        QTest::newRow("every gate open, answer read") << true << true << true << false << true << read << wider << true;
        // A late ack of an older configure, or a frame a queued resize is
        // about to replace: never passed off as the answer to the newest
        // offer, and never centred, since the centring move would retarget
        // the queued resize to a displaced position (F49).
        QTest::newRow("unsettled") << true << true << true << false << false << owed << wider << false;
        // KWin's own maximize, fullscreen or quick tile: the frame comes
        // before the state that explains it (F123).
        QTest::newRow("KWin state requested") << true << true << false << false << true << owed << wider << false;
        // The user's resize, until its drain: the user's frame (F35).
        QTest::newRow("resize held") << true << true << true << true << true << owed << wider << false;
        // KWin or the user resized it since the offer (F141).
        QTest::newRow("foreign resize") << true << true << true << false << true << foreign << wider << false;
        // Floating, untracked, or under a user move or resize.
        QTest::newRow("not scroll-managed") << false << true << true << false << true << owed << wider << false;
        // The offer is in coordinates the output has since left (F815).
        QTest::newRow("coordinates stale") << true << false << true << false << true << owed << wider << false;
        // A frame that left the column was moved elsewhere (KWin's Move to
        // Screen).
        QTest::newRow("left the column") << true << true << true << false << true << owed << QRect(3000, 8, 1908, 2052)
                                         << false;
        // An empty frame is a mid-unmap commit. The interior rows are the ones
        // that need the predicate's emptiness term: QRect::intersects is true
        // for an empty rect strictly inside the column, and false for the one
        // on its left edge.
        QTest::newRow("empty frame on the column's edge")
            << true << true << true << false << true << owed << QRect(8, 8, 0, 2052) << false;
        QTest::newRow("zero-width frame inside the column")
            << true << true << true << false << true << owed << QRect(100, 8, 0, 2052) << false;
        QTest::newRow("zero-height frame inside the column")
            << true << true << true << false << true << owed << QRect(100, 100, 500, 0) << false;
    }

    void columnAnswerReadabilityTable()
    {
        QFETCH(bool, managed);
        QFETCH(bool, coordinatesHold);
        QFETCH(bool, kwinStateAtRest);
        QFETCH(bool, resizeHeld);
        QFETCH(bool, settled);
        QFETCH(int, answer);
        QFETCH(QRect, live);
        QFETCH(bool, reads);
        ColumnAnswerGates gates;
        gates.managed = managed;
        gates.coordinatesHold = coordinatesHold;
        gates.kwinStateAtRest = kwinStateAtRest;
        gates.resizeHeld = resizeHeld;
        gates.settled = settled;
        QCOMPARE(readsAsColumnAnswer(gates, static_cast<ColumnAnswer>(answer), QRect(8, 8, 1908, 2052), live), reads);
    }

    // An answer is reported once. Owed always reads as new, whatever size it
    // is, because the offer it answers is new. Once Read, only a size the
    // client moves to by itself is new: a pure move that keeps the size, or
    // KWin's keep-visible clamp, must not re-send the same answer.
    void columnAnswerNewness()
    {
        const QSize answered(2880, 2052);
        QVERIFY(isNewColumnAnswer(ColumnAnswer::Owed, answered, answered));
        QVERIFY(isNewColumnAnswer(ColumnAnswer::Owed, QSize(), answered));
        QVERIFY(!isNewColumnAnswer(ColumnAnswer::Read, answered, answered));
        QVERIFY(isNewColumnAnswer(ColumnAnswer::Read, answered, QSize(3000, 2052)));
        QVERIFY(!isNewColumnAnswer(ColumnAnswer::Foreign, answered, QSize(3000, 2052)));
    }

    // Which readable answers reach the engine. Along the strip a difference
    // either way; across it only a longer answer, which hangs past the work
    // area; within a pixel, never.
    void committedSizeReportTable_data()
    {
        QTest::addColumn<QSize>("offered");
        QTest::addColumn<QSize>("committed");
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<bool>("reports");

        const QSize column(1908, 2052);
        // The shape that prompted this: wider along a horizontal strip, a
        // little short across it.
        QTest::newRow("wider along the strip") << column << QSize(2880, 1948) << false << true;
        QTest::newRow("narrower along the strip") << column << QSize(1500, 2052) << false << true;
        // Shorter across the strip only: the commit path already centres it,
        // and reporting would send the engine a no-op on every placement.
        QTest::newRow("shorter across, horizontal strip") << column << QSize(1908, 1948) << false << false;
        // Longer across the strip, main axis equal: it hangs past the work
        // area's edge, which is the engine's to judge (F166).
        QTest::newRow("longer across, horizontal strip") << column << QSize(1908, 2200) << false << true;
        QTest::newRow("matches the offer") << column << column << false << false;
        // The axis decides which dimension is the main one. The pairs below
        // mirror the horizontal ones, so each axis's arm is driven in both
        // directions and a flipped comparison on either fails a row.
        QTest::newRow("longer across, vertical strip") << column << QSize(2880, 2052) << true << true;
        QTest::newRow("shorter across, vertical strip") << column << QSize(1500, 2052) << true << false;
        QTest::newRow("taller along a vertical strip") << column << QSize(1908, 2400) << true << true;
        QTest::newRow("shorter along a vertical strip") << column << QSize(1908, 1948) << true << true;
        // A pixel either way is decoration rounding at a fractional scale,
        // never an answer (F52); two pixels are.
        QTest::newRow("a pixel wider along the strip") << column << QSize(1909, 2052) << false << false;
        QTest::newRow("a pixel narrower along the strip") << column << QSize(1907, 2052) << false << false;
        QTest::newRow("a pixel longer across the strip") << column << QSize(1908, 2053) << false << false;
        QTest::newRow("two pixels wider along the strip") << column << QSize(1910, 2052) << false << true;
        QTest::newRow("two pixels longer across the strip") << column << QSize(1908, 2054) << false << true;
        // Degenerate sizes carry no answer (a mid-unmap commit, a column not
        // yet known). The zero-height row pins isEmpty rather than isNull,
        // which would let a size with one zero extent through.
        QTest::newRow("empty commit") << column << QSize(0, 0) << false << false;
        QTest::newRow("zero-height commit") << column << QSize(2880, 0) << false << false;
        QTest::newRow("empty offer") << QSize() << QSize(2880, 1948) << false << false;
    }

    void committedSizeReportTable()
    {
        QFETCH(QSize, offered);
        QFETCH(QSize, committed);
        QFETCH(bool, vertical);
        QFETCH(bool, reports);
        QCOMPARE(shouldReportCommittedSize(offered, committed, vertical), reports);
    }
};

QTEST_APPLESS_MAIN(TestScrollDecisions)
#include "test_scroll_decisions.moc"

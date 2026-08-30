// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the scroll-managed window decision helpers
// (tilinghandler/scrolldecisions.h) — the windowed-fullscreen 5-way batch
// decision (with its clear-in-flight marker arm/consume contract), the
// column-maximize 3-way batch decision, the counter-assert burst budget,
// and the compositor-claim release table (which claim answers to which exit
// scope, the teardown ordering rule, and the retain-on-fullscreen-skip
// policy). Same header-only reach as test_anchor_uniforms:
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
    // and it is the half that needs the marker. Keep both.
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
            const MaximizeAction action =
                resolveMaximizeToEdgesAction(flagOnWire, inSet, kwinMax, /*toggleInFlight=*/false);
            // MODELS what the batch arm does with the answer — it does not
            // call it. kwin-effect has no linkable test target, so the arm's
            // own bookkeeping cannot be driven from here; only a wrong
            // resolver fails this test, not a wrong arm. Keep this lambda in
            // step with tiling.cpp's Apply/Release block by hand. It models
            // the UNCONDITIONAL path only, and knowingly diverges twice: the
            // real arm skips the compositor call for a fullscreen window or
            // one mid user move/resize, and releaseMaximizedToEdges RETAINS
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
            const MaximizeAction action = resolveMaximizeToEdgesAction(flagOnWire, inSet, kwinMax, armed);
            if (action == MaximizeAction::Apply) {
                inSet = true;
                kwinMax = true;
            } else if (action == MaximizeAction::Release) {
                inSet = false;
                kwinMax = false;
            }
            return action;
        };

        // The user clicks restore. KWin clears its own bit and the effect
        // writes nothing; the toggle is dispatched and now in flight.
        kwinMax = false;
        armed = true;

        // A batch the daemon emitted before it dequeued the toggle still
        // carries flag=true. Unsuppressed this is Apply, which re-maximizes
        // the window mid-flight and commits the stale maximized rect with it.
        // THIS is the assertion that fails without the marker.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::None));
        QVERIFY(inSet);
        QVERIFY(!kwinMax);

        // More than one pre-toggle batch can be in flight (batches arrive at
        // wheel-tick rate on a scrolling strip), so the marker must survive
        // the first one rather than being consumed by it.
        QCOMPARE(static_cast<int>(step(true)), static_cast<int>(MaximizeAction::None));
        QVERIFY(inSet);

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

    // The marker must never swallow the engine's own answer. An adopt for a
    // window this effect instance has never seen (daemon still holding the
    // state across an effect restart) has to land even while armed, or the
    // restart repair the Apply arm exists for would be lost.
    void markerNeverSuppressesTheEnginesAnswer()
    {
        QCOMPARE(static_cast<int>(resolveMaximizeToEdgesAction(true, false, false, /*toggleInFlight=*/true)),
                 static_cast<int>(MaximizeAction::Apply));
        QCOMPARE(static_cast<int>(resolveMaximizeToEdgesAction(true, false, true, /*toggleInFlight=*/true)),
                 static_cast<int>(MaximizeAction::Apply));
        QCOMPARE(static_cast<int>(resolveMaximizeToEdgesAction(false, true, false, /*toggleInFlight=*/true)),
                 static_cast<int>(MaximizeAction::Release));
        QCOMPARE(static_cast<int>(resolveMaximizeToEdgesAction(false, true, true, /*toggleInFlight=*/true)),
                 static_cast<int>(MaximizeAction::Release));
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

        // Two monocle blanks follow, and both are deliberate. UntrackFunnel
        // matches StripExit except for monocle, whose blank is RECORDED AS
        // UNRESOLVED rather than decided (see the enum comment): monocle rides
        // cleanupClosedWindowState's bare scrub on that path instead.
        // PassiveFloat's monocle blank is DECIDED: re-driving a maximize
        // restore from a passive float signal has not been shown safe against
        // the monocle batch that owns that membership.
        QTest::newRow("untrack/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::UntrackFunnel) << false;
        QTest::newRow("untrack/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::UntrackFunnel) << true;
        QTest::newRow("untrack/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::UntrackFunnel) << true;

        QTest::newRow("passiveFloat/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::PassiveFloat) << false;
        QTest::newRow("passiveFloat/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::PassiveFloat) << true;
        QTest::newRow("passiveFloat/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::PassiveFloat) << true;

        QTest::newRow("modeFlip/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::ModeFlip) << true;
        QTest::newRow("modeFlip/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::ModeFlip) << true;
        QTest::newRow("modeFlip/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::ModeFlip) << true;

        // FullscreenExitWhileFloating — windowed fullscreen cannot be held
        // here by construction; the arm runs BECAUSE the window left it.
        QTest::newRow("fsExit/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::FullscreenExitWhileFloating)
                                        << true;
        QTest::newRow("fsExit/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::FullscreenExitWhileFloating)
                                    << false;
        QTest::newRow("fsExit/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::FullscreenExitWhileFloating)
                                       << true;

        QTest::newRow("teardown/monocle") << c(Claim::MonocleMaximize) << s(ClaimScope::Teardown) << true;
        QTest::newRow("teardown/wfs") << c(Claim::WindowedFullscreen) << s(ClaimScope::Teardown) << true;
        QTest::newRow("teardown/column") << c(Claim::MaximizedToEdges) << s(ClaimScope::Teardown) << true;
    }

    void claimReleaseTable()
    {
        QFETCH(int, claim);
        QFETCH(int, scope);
        QFETCH(bool, releases);
        QCOMPARE(claimReleasesOn(static_cast<Claim>(claim), static_cast<ClaimScope>(scope)), releases);
    }

    void teardownReleasesFullscreenBeforeEitherMaximize()
    {
        // Ordering, not preference. Both maximize releases SKIP a window that
        // still holds fullscreen, and on X11 setFullScreen(false) has landed
        // by the time the next claim runs — so fullscreen first is what lets
        // a window holding both states get a real restore instead of a skip.
        // The reverse order was a live regression during PR #994's
        // remediation, which is why this is pinned rather than left to the
        // enum's declaration order.
        QVERIFY(claimReleaseOrder(Claim::WindowedFullscreen) < claimReleaseOrder(Claim::MonocleMaximize));
        QVERIFY(claimReleaseOrder(Claim::WindowedFullscreen) < claimReleaseOrder(Claim::MaximizedToEdges));
    }

    void onlyTheMaximizeClaimsRetainOnAFullscreenSkip()
    {
        // Shedding an entry whose bit was never handed back strands that bit
        // with nothing recording it is owed. Windowed fullscreen is the
        // exception because its caller sheds membership before the
        // compositor half runs at all.
        QVERIFY(claimRetainsOnFullscreenSkip(Claim::MonocleMaximize));
        QVERIFY(claimRetainsOnFullscreenSkip(Claim::MaximizedToEdges));
        QVERIFY(!claimRetainsOnFullscreenSkip(Claim::WindowedFullscreen));
    }
};

QTEST_APPLESS_MAIN(TestScrollDecisions)
#include "test_scroll_decisions.moc"

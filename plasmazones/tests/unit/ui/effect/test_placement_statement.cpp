// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>

#include "plasmazoneseffect/placementstatement.h"

using PlasmaZones::PlacementStatement::decide;
using PlasmaZones::PlacementStatement::fullscreenBails;
using PlasmaZones::PlacementStatement::Inputs;
using PlasmaZones::PlacementStatement::Purpose;
using PlasmaZones::PlacementStatement::Verdict;

namespace {
Inputs inputs(Purpose purpose)
{
    Inputs in;
    in.purpose = purpose;
    return in;
}
} // namespace

/**
 * @brief The pure placement-statement decisions: what a placement does to a
 *        window's KWin fullscreen and maximize state.
 */
class TestPlacementStatement : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A window that requested fullscreen is fullscreen before the client
    // commits it: the apply bails (F496). The old gate tested the committed
    // bit first and moveResized a window on its way into fullscreen.
    void aRequestedFullscreenBailsBeforeTheClientCommits()
    {
        QVERIFY(fullscreenBails(/*hasKWinWindow=*/true, /*committed=*/false, /*requested=*/true, /*member=*/false));
    }

    // The windowed-fullscreen exit: requested off while the committed bit
    // drains. The restoring rect must land.
    void aRequestedExitDoesNotBail()
    {
        QVERIFY(!fullscreenBails(true, /*committed=*/true, /*requested=*/false, false));
    }

    // A windowed-fullscreen member holds fullscreen at its column rect on
    // purpose: its applies never bail.
    void aWindowedFullscreenMemberDoesNotBail()
    {
        QVERIFY(!fullscreenBails(true, true, true, /*member=*/true));
    }

    // Without a KWin window the committed bit decides.
    void withoutAKWinWindowTheCommittedStateDecides()
    {
        QVERIFY(fullscreenBails(/*hasKWinWindow=*/false, /*committed=*/true, /*requested=*/false, false));
        QVERIFY(!fullscreenBails(false, false, true, false));
    }

    // A fullscreen window the user activates is reported on a scrolling and a
    // snapping screen, so a key acts on it (F522); autotile keeps leaving it
    // out.
    void fullscreenActivationIsReportedOnSnappingScreens()
    {
        using PlasmaZones::PlacementStatement::reportsFullscreenActivation;
        QVERIFY(reportsFullscreenActivation(/*scrolling=*/false, /*managed=*/false));
        QVERIFY(reportsFullscreenActivation(true, true));
        QVERIFY(!reportsFullscreenActivation(false, true));
    }

    // A user verb on a fullscreen window ends the fullscreen, so its apply
    // lands instead of bailing (F505, F524). The old demote returned for a
    // window that was not maximized and never touched fullscreen.
    void userVerbEndsFullscreen()
    {
        Inputs in = inputs(Purpose::UserVerb);
        in.requestedFullScreen = true;
        const Verdict v = decide(in);
        QVERIFY(v.endFullScreen);
        QVERIFY(!v.endMaximize);
    }

    // Fullscreen over a maximize: both end (the effect writes fullscreen first).
    void userVerbEndsFullscreenThenMaximize()
    {
        Inputs in = inputs(Purpose::UserVerb);
        in.requestedFullScreen = true;
        in.maximized = true;
        const Verdict v = decide(in);
        QVERIFY(v.endFullScreen);
        QVERIFY(v.endMaximize);
    }

    // A tiling engine's claim is shed for any purpose: the window is leaving
    // the stack or strip that owns it (F342). The old demote returned early.
    void engineClaimIsShedForAnyPurpose()
    {
        for (const Purpose purpose : {Purpose::UserVerb, Purpose::Restatement}) {
            Inputs in = inputs(purpose);
            in.maximized = true;
            in.engineMaximizeClaim = true;
            const Verdict v = decide(in);
            QVERIFY(v.shedEngineMaximize);
            QVERIFY2(!v.endMaximize, "the claim's own release ends the maximize");
        }
    }

    // A windowed-fullscreen member placed into a snap zone is handed back,
    // not exempted (F529).
    void windowedFullscreenMemberIsHandedBack()
    {
        Inputs in = inputs(Purpose::UserVerb);
        in.requestedFullScreen = true;
        in.windowedFsMember = true;
        const Verdict v = decide(in);
        QVERIFY(v.shedWindowedFullscreen);
        QVERIFY2(!v.endFullScreen, "the claim's release ends it");
    }

    // A user verb's free apply (Restore, the float toggle) ends a maximize, so
    // the window lands at its float-back (F582).
    void userVerbEndsAMaximize()
    {
        Inputs in = inputs(Purpose::UserVerb);
        in.maximized = true;
        QVERIFY(decide(in).endMaximize);
    }

    // A re-statement on the same output keeps a maximize: no apply, the zone
    // seated as the restore rect (F490, F509, F547). The old demote ended it
    // and moved the window.
    void restatementKeepsTheMaximizeAndSeatsTheZone()
    {
        Inputs in = inputs(Purpose::Restatement);
        in.maximized = true;
        const Verdict v = decide(in);
        QVERIFY(!v.apply);
        QVERIFY(v.seatMaximizeRestore);
        QVERIFY(!v.endMaximize);
        QVERIFY(!v.seatFullScreenRestore);
    }

    // ...and a fullscreen: no apply, the zone seated as the fullscreen restore
    // rect (F505(b)). The old apply bailed and seated nothing.
    void restatementKeepsFullscreenAndSeatsTheZone()
    {
        Inputs in = inputs(Purpose::Restatement);
        in.requestedFullScreen = true;
        const Verdict v = decide(in);
        QVERIFY(!v.apply);
        QVERIFY(v.seatFullScreenRestore);
        QVERIFY(!v.endFullScreen);
    }

    // Maximized, then fullscreen on top: only the maximize restore is seated,
    // KWin keeps its own fullscreen restore (F524(b)).
    void restatementOfMaximizedThenFullscreenSeatsOnlyTheMaximizeRestore()
    {
        Inputs in = inputs(Purpose::Restatement);
        in.maximized = true;
        in.requestedFullScreen = true;
        const Verdict v = decide(in);
        QVERIFY(!v.apply);
        QVERIFY(v.seatMaximizeRestore);
        QVERIFY(!v.seatFullScreenRestore);
    }

    // A re-statement onto another output leaves the state and moves, like a
    // user verb (F576, F561).
    void crossOutputRestatementLeavesAndMoves()
    {
        Inputs in = inputs(Purpose::Restatement);
        in.maximized = true;
        in.requestedFullScreen = true;
        in.sameOutput = false;
        const Verdict v = decide(in);
        QVERIFY(v.apply);
        QVERIFY(v.endFullScreen);
        QVERIFY(v.endMaximize);
        QVERIFY(!v.seatMaximizeRestore && !v.seatFullScreenRestore);
    }

    // An engine claim shed by a re-statement leaves nothing to keep: it applies.
    void restatementShedsAClaimAndApplies()
    {
        Inputs in = inputs(Purpose::Restatement);
        in.maximized = true;
        in.engineMaximizeClaim = true;
        const Verdict v = decide(in);
        QVERIFY(v.shedEngineMaximize);
        QVERIFY(v.apply);
    }

    // A plain window re-stated: nothing to keep or hand back.
    void plainRestatementApplies()
    {
        const Verdict v = decide(inputs(Purpose::Restatement));
        QVERIFY(v.apply);
        QVERIFY(!v.seatMaximizeRestore && !v.seatFullScreenRestore && !v.endMaximize && !v.endFullScreen);
    }

    // A live gesture the placement does not own: nothing is written, the
    // replay decides again.
    void aLiveGestureWritesNothing()
    {
        Inputs in = inputs(Purpose::UserVerb);
        in.maximized = true;
        in.requestedFullScreen = true;
        in.engineMaximizeClaim = true;
        in.gestureLive = true;
        const Verdict v = decide(in);
        QVERIFY(!v.endFullScreen && !v.endMaximize && !v.shedEngineMaximize && !v.shedWindowedFullscreen);
    }
};

QTEST_GUILESS_MAIN(TestPlacementStatement)
#include "test_placement_statement.moc"

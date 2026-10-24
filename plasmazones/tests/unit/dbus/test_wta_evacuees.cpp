// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_evacuees.cpp
 * @brief The daemon's evacuee ledger: what reportOutputSettle answers for a
 *        window KWin moved off an output that went away, and when that output
 *        comes back. A window returned untouched goes back into its zone; one
 *        the user touched, or that closed, is not re-seated; a PlasmaZones
 *        placement while the output was away is re-asserted. A window
 *        announced on its parked output is re-seated there, and the re-seat
 *        is said ahead of the park's drop: alone while another context stays
 *        parked there, and not at all when nothing was seated (F1147).
 */

#include "wta_convenience_fixture.h"
#include "helpers/StubPlacementEngine.h"

#include <QScopeGuard>

using PhosphorProtocol::OutputSettleRow;
using PhosphorProtocol::OutputSettleVerdict;

namespace {
const QString kA = QStringLiteral("DP-1");
const QString kB = QStringLiteral("DP-2");
const QString kUuidA = QStringLiteral("uuid-a");
const QString kUuidB = QStringLiteral("uuid-b");

/// KWin moved the window off A, which disconnected, onto B. S0 is the state
/// it had on A.
OutputSettleRow unplugRow(const QString& windowId)
{
    OutputSettleRow row;
    row.windowId = windowId;
    row.outputUuid = kUuidB;
    row.screenId = kB;
    row.sourceScreenId = kA;
    row.sourceConnected = false;
    row.x = 2000;
    row.y = 100;
    row.width = 800;
    row.height = 600;
    row.moveResizeCount = 3;
    row.hasS0 = true;
    row.s0Uuid = kUuidA;
    row.s0ScreenId = kA;
    row.s0MoveResizeCount = 3;
    return row;
}

/// A came back and KWin moved the window back onto it from B.
OutputSettleRow redockRow(const QString& windowId, int moveResizeCount = 3)
{
    OutputSettleRow row = unplugRow(windowId);
    row.outputUuid = kUuidA;
    row.screenId = kA;
    row.sourceScreenId = kB;
    row.sourceConnected = true;
    row.x = 100;
    row.moveResizeCount = moveResizeCount;
    row.kwinOnly = moveResizeCount == row.s0MoveResizeCount;
    return row;
}

int verdictOf(const PhosphorProtocol::OutputSettleVerdictList& verdicts, const QString& windowId)
{
    for (const OutputSettleVerdict& v : verdicts) {
        if (v.windowId == windowId) {
            return v.verdict;
        }
    }
    return -1;
}
} // namespace

class TestWtaEvacuees : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
        installPerScreenResolver();
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_snapEngine->setCurrentDesktopForScreen(kB, 1);
    }
    void cleanup()
    {
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        cleanupFixture();
    }

    // The basic round trip: parked when A goes away (its record no longer
    // names A's zone), an evacuee while away, and back into its zone when KWin
    // returns it untouched, with the park dropped.
    void untouchedWindowReturnsToItsZone()
    {
        const QString w = QStringLiteral("app|round-trip");
        QVERIFY(snapOnA(w));
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));
        QVERIFY(unplugA());
        QVERIFY(m_snapEngine->hasParked(w, kA));
        const auto rec = m_wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));

        QCOMPARE(verdictOf(m_wta->reportOutputSettle({unplugRow(w)}), w), int(OutputSettleVerdict::EvacueeFloat));
        QVERIFY(m_snapEngine->hasParked(w, kA));

        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({redockRow(w)}), w), int(OutputSettleVerdict::Readopt));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[0]);
        QVERIFY(!m_snapEngine->hasParked(w, QString()));
        QCOMPARE(dropped.count(), 1);
        QCOMPARE(dropped.first().at(1).toString(), kUuidA);
    }

    // A window the user moved or resized while A was away is not re-seated:
    // its crossing is the user's move, and the park goes.
    void touchedWindowIsTheUsersMove()
    {
        const QString w = QStringLiteral("app|touched");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_wta->reportOutputSettle({unplugRow(w)});

        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({redockRow(w, 4)}), w), int(OutputSettleVerdict::UserMove));
        QVERIFY(m_wta->service()->zoneForWindow(w).isEmpty());
        QVERIFY(!m_snapEngine->hasParked(w, QString()));
        QCOMPARE(dropped.count(), 1);
    }

    // A PlasmaZones snap on B while A was away is a placement, not a touch to
    // throw away: the park goes, and when KWin pulls the window back to A the
    // settle re-asserts where PlasmaZones put it.
    void snapCommitWhileAwayIsReasserted()
    {
        const QString w = QStringLiteral("app|placed-on-b");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_wta->reportOutputSettle({unplugRow(w)});

        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        QVERIFY(!m_snapEngine->hasParked(w, QString()));

        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({redockRow(w)}), w), int(OutputSettleVerdict::Reassert));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[1]);
    }

    // The re-assert re-applies a zone only where snapping runs the screen it
    // is stored for. Snap keeps frozen memory on a screen a tiling engine took
    // over, and its zone rect would fight the tile there (F399). Mutation:
    // drop the isActiveOnScreen gate in reassertEvacuee and the zone rect on B
    // is applied again.
    void aZoneOnAScreenSnappingNoLongerRunsIsNotReapplied()
    {
        const QString w = QStringLiteral("app|frozen-zone");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_wta->reportOutputSettle({unplugRow(w)});
        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        m_snapEngine->setLiveModeResolver([](const QString& screenId) {
            return screenId == kB ? PhosphorZones::AssignmentEntry::Scrolling
                                  : PhosphorZones::AssignmentEntry::Snapping;
        });
        const auto restore = qScopeGuard([this] {
            m_snapEngine->setLiveModeResolver({});
        });
        QCOMPARE(m_snapEngine->zoneForWindow(w), m_zoneIds[1]);

        QSignalSpy applySpy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_wta->reportOutputSettle({redockRow(w)});

        for (const QList<QVariant>& args : std::as_const(applySpy)) {
            QVERIFY2(args.at(5).toString().isEmpty(), "no zone may be re-applied on a screen snapping does not run");
        }
    }

    // A window that closes while A is away has nothing left to return.
    void closeDropsThePark()
    {
        const QString w = QStringLiteral("app|closed");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        m_wta->windowClosed(w, 0, kB);
        QVERIFY(!m_snapEngine->hasParked(w, QString()));
        QCOMPARE(dropped.count(), 1);
    }

    // A snap float that cannot be placed yet (minimized, or on a desktop out
    // of view) floats on the snapping output at the settle: a snap float
    // applies no geometry, so nothing waits for an arrival the snap channel
    // may never see (F643, F1047).
    void aMinimizedSnapFloatEvacueeFloatsAtTheSettle()
    {
        const QString w = QStringLiteral("app|minimized-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(floatOnA(w));
        QVERIFY(unplugA());
        OutputSettleRow row = unplugRow(w);
        row.placeableNow = false;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));
        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), kB);
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // One on another activity waits, and floats at its arrival on B, once.
    void aSnapFloatOnAnotherActivityFloatsAtItsArrival()
    {
        const QString w = QStringLiteral("app|other-activity-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendSnapFloatOnB(w));
        QVERIFY(m_wta->adoptPendingEvacueeFloat(w, kB));
        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), kB);
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // A snapped (not floating) evacuee out of view on a snapping output is
    // owed no float: snapping floats only a window it held floating (F1047).
    // Nothing waits either: a tiling engine on B by the arrival would take a
    // float left owed.
    void aSnappedEvacueeOutOfViewOnASnappingOutputOwesNoFloat()
    {
        const QString w = QStringLiteral("app|snapped-hidden-evacuee");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        OutputSettleRow row = unplugRow(w);
        row.placeableNow = false;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));
        QVERIFY(!m_snapEngine->isFloating(w));
        StubPlacementEngine tile;
        tile.activeScreens = {kB};
        m_wta->setEngines(m_snapEngine, &tile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
        QVERIFY(tile.received.isEmpty());
    }

    // The float is owed only on the output the window landed on: an arrival
    // anywhere else spends it without floating (F1047).
    void aPendingFloatIsOwedOnlyWhereItLanded()
    {
        const QString w = QStringLiteral("app|moved-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendSnapFloatOnB(w));
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kA));
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // One whose own context's mode could not be told (another activity)
    // floats as an evacuee of the mode it arrives in: in place on B when a
    // tiling engine runs it by the arrival (evacuee_floats, F1105).
    void aPendingFloatOfUnknownModeFloatsInPlaceOnATilingArrival()
    {
        const QString w = QStringLiteral("app|unknown-mode-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendSnapFloatOnB(w));
        StubPlacementEngine tile;
        tile.activeScreens = {kB};
        m_wta->setEngines(m_snapEngine, &tile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        QVERIFY(m_wta->adoptPendingEvacueeFloat(w, kB));
        QCOMPARE(tile.received.size(), 1);
        QVERIFY(tile.received.first().wasFloating);
        QVERIFY2(!m_wta->adoptPendingEvacueeFloat(w, kB), "the float is owed once");
    }

    // The mode is the window's own context's, not the one in view: an
    // evacuee on a hidden desktop that tiles on B waits for its arrival
    // there while the desktop in view snaps, and is not stated a snap float
    // in that desktop's store (F1105).
    void aHiddenEvacueeOnATilingDesktopFloatsAtItsArrivalThere()
    {
        const QString w = QStringLiteral("app|hidden-tiling-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendOnTilingDesktopOfB(w));
        const auto key = m_snapEngine->heldKeyForWindow(w);
        QVERIFY2(!key || key->screenId != kB, "no snap float stated on B's desktop 2");
        StubPlacementEngine tile;
        tile.activeScreens = {kB};
        m_wta->setEngines(m_snapEngine, &tile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        QVERIFY(m_wta->adoptPendingEvacueeFloat(w, kB));
        QCOMPARE(tile.received.size(), 1);
        QVERIFY(tile.received.first().wasFloating);
        QVERIFY2(!m_wta->adoptPendingEvacueeFloat(w, kB), "the float is owed once");
    }

    // And that tiling float is spent, not floated, when it arrives where
    // snapping runs (float is per mode, F1047): a later arrival on B, with a
    // tiling engine running it by then, is owed nothing either.
    void aPendingTilingFloatIsNotOwedOnASnappingArrival()
    {
        const QString w = QStringLiteral("app|mode-flipped-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendOnTilingDesktopOfB(w));
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
        const auto key = m_snapEngine->heldKeyForWindow(w);
        QVERIFY(!key || key->screenId != kB);

        StubPlacementEngine tile;
        tile.activeScreens = {kB};
        m_wta->setEngines(m_snapEngine, &tile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        QVERIFY2(!m_wta->adoptPendingEvacueeFloat(w, kB), "the first arrival spent the float");
        QVERIFY(tile.received.isEmpty());
    }

    // A hidden desktop assigned tiling that cannot tile (the feature switched
    // off, or the context disabled) runs snapping, as the daemon's resolver
    // says: the snap float is stated in that desktop's store at the settle,
    // not left waiting for a tiling arrival that is never owed it (F1122).
    void aHiddenEvacueeOnADesktopThatCannotTileIsStatedASnapFloat()
    {
        const QString w = QStringLiteral("app|tiling-off-evacuee");
        const auto clear = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
            m_wta->setContextModeResolver({});
        });
        // Only the window's own context snaps: a resolver asked about any other
        // context (the desktop in view, the output it left) answers Autotile,
        // so a wrong key fails the row.
        m_wta->setContextModeResolver(
            [activity = m_layoutManager->currentActivity()](const QString& screen, int desktop, const QString& act) {
                return screen == kB && desktop == 2 && act == activity ? PhosphorZones::AssignmentEntry::Snapping
                                                                       : PhosphorZones::AssignmentEntry::Autotile;
            });
        QVERIFY(pendOnTilingDesktopOfB(w));
        const auto key = m_snapEngine->heldKeyForWindow(w);
        QVERIFY(key.has_value());
        QCOMPARE(key->screenId, kB);
        QCOMPARE(key->desktop, 2);
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // The reverse: B tiles in view, but the window's own hidden desktop there
    // snaps, so its snap float is stated in that desktop's store at the
    // settle and nothing waits (F1105).
    void aHiddenSnapFloatOnASnappingDesktopIsStatedThere()
    {
        const QString w = QStringLiteral("app|hidden-snapping-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(floatOnA(w));
        placeOnDesktop(w, 2);
        QVERIFY(unplugA());
        StubPlacementEngine tile;
        tile.activeScreens = {kB};
        m_wta->setEngines(m_snapEngine, &tile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        OutputSettleRow row = unplugRow(w);
        row.placeableNow = false;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));
        const auto key = m_snapEngine->heldKeyForWindow(w);
        QVERIFY(key.has_value());
        QCOMPARE(key->screenId, kB);
        QCOMPARE(key->desktop, 2);
        QVERIFY(tile.received.isEmpty());
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // KWin returned the window untouched: it is back in its place, and the
    // float it could not take while away is no longer owed (F1046).
    void aReadoptSpendsThePendingFloat()
    {
        const QString w = QStringLiteral("app|returned-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(pendSnapFloatOnB(w));
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_wta->reportOutputSettle({redockRow(w)});
        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));
    }

    // A PlasmaZones placement while A was away is re-asserted only when KWin
    // pulls the window back onto A: a crossing anywhere else is the user's
    // (F1049).
    void aPlacementMarkReassertsOnlyAPullBackOntoTheAwayOutput()
    {
        const QString w = QStringLiteral("app|placed-then-moved");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_wta->reportOutputSettle({unplugRow(w)});
        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        OutputSettleRow moved = redockRow(w, 4);
        moved.screenId = QStringLiteral("DP-3");
        moved.outputUuid = QStringLiteral("uuid-c");
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({moved}), w), int(OutputSettleVerdict::UserMove));
    }

    // A mark is per output. A window placed while A was away, then parked for
    // B and placed again while B was away, is marked for both; KWin pulling it
    // back onto A re-asserts and leaves the mark for B, whose pull-back is
    // re-asserted too (F1107).
    void aPlacementMarkKeepsEveryAwayOutputUntilItsPullBack()
    {
        const QString w = QStringLiteral("app|placed-twice");
        const QString kC = QStringLiteral("DP-3");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        QVERIFY(!m_wta->parkOutput(kB).isEmpty());
        m_snapEngine->pruneStatesForRemovedScreen(kB);
        m_wta->releaseParkedSlots(kB);
        m_snapEngine->setCurrentDesktopForScreen(kC, 1);
        m_snapEngine->commitSnap(w, m_zoneIds[2], kC);

        OutputSettleRow ontoA = redockRow(w, 4);
        ontoA.sourceScreenId = kC;
        ontoA.baseX = 4000;
        ontoA.baseWidth = 800;
        ontoA.baseHeight = 600;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({ontoA}), w), int(OutputSettleVerdict::Reassert));
        OutputSettleRow ontoB = ontoA;
        ontoB.screenId = kB;
        ontoB.outputUuid = kUuidB;
        ontoB.sourceScreenId = kA;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({ontoB}), w), int(OutputSettleVerdict::Reassert));
    }

    // A window that closes drops its placement mark with its park (F1049).
    void closeDropsThePlacementMark()
    {
        const QString w = QStringLiteral("app|placed-then-closed");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_wta->reportOutputSettle({unplugRow(w)});
        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        m_wta->windowClosed(w, 0, kB);
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({redockRow(w, 4)}), w), int(OutputSettleVerdict::UserMove));
    }

    // A window snap held floating on A keeps floating on B, a snapping
    // output, when A goes away (F670). The prune drops A's stores and the float
    // with them, so the settle reads the float the park saw. A second settle
    // naming the same untouched window leaves it as it is.
    void aSnapFloatEvacueeFloatsOnTheSnappingOutput()
    {
        const QString w = QStringLiteral("app|float-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(floatOnA(w));
        QVERIFY(unplugA());
        for (int settle = 0; settle < 2; ++settle) {
            const QByteArray which = QByteArray("settle ") + QByteArray::number(settle);
            QVERIFY2(verdictOf(m_wta->reportOutputSettle({unplugRow(w)}), w) == int(OutputSettleVerdict::EvacueeFloat),
                     which.constData());
            QVERIFY2(m_snapEngine->isFloating(w), which.constData());
            QVERIFY2(m_snapEngine->screenForTrackedWindow(w) == kB, which.constData());
        }
    }

    // The same for a window the user touched while A went away: the touched
    // arm drops the park first, and the float it saw still decides (F670).
    void aTouchedSnapFloatEvacueeStillFloatsOnB()
    {
        const QString w = QStringLiteral("app|touched-float-evacuee");
        const auto clearResolver = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
        });
        QVERIFY(floatOnA(w));
        QVERIFY(unplugA());
        OutputSettleRow row = unplugRow(w);
        row.moveResizeCount = 4;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));
        QVERIFY(!m_snapEngine->hasParked(w, QString()));
        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), kB);
    }

    // A snap float KWin moves onto an output whose desktop in view is the
    // tiling opt-out ("autotile:none") lands not floating: no engine owns a
    // float there, and the effect hears so. Its park stays for A's return (user
    // decision autotile_none_float_owner). Mutation: drop the context test in
    // SnapEngine::handoffReceive and snap floats it on B.
    void aSnapFloatEvacueeOnTheOptOutLandsNotFloating()
    {
        const QString w = QStringLiteral("app|opt-out-evacuee");
        const auto clear = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
            m_snapEngine->setContextModeResolver({});
        });
        QVERIFY(floatOnA(w));
        optOutOnB(1);
        QVERIFY(unplugA());
        QSignalSpy floatSpy(m_wta, &WindowTrackingAdaptor::windowFloatingChanged);

        QCOMPARE(verdictOf(m_wta->reportOutputSettle({unplugRow(w)}), w), int(OutputSettleVerdict::EvacueeFloat));

        QVERIFY(!m_snapEngine->isFloating(w));
        QVERIFY(!m_wta->service()->isWindowFloating(w));
        QVERIFY(!floatSpy.isEmpty());
        QVERIFY(!floatSpy.last().at(1).toBool());
        QVERIFY2(m_snapEngine->hasParked(w, kA), "the park waits for A's return");
    }

    // The same on a hidden desktop of B given the opt-out. The daemon's own
    // resolver says it runs snapping (nothing tiles there), so the settle
    // states the evacuee in that desktop's store at once, and it holds no
    // float there either. Mutation: drop the context test in
    // SnapEngine::handoffReceive and snap floats it on B's desktop 2.
    void aHiddenSnapFloatEvacueeOnTheOptOutHoldsNoFloat()
    {
        const QString w = QStringLiteral("app|hidden-opt-out-evacuee");
        const auto clear = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
            m_wta->setContextModeResolver({});
            m_snapEngine->setContextModeResolver({});
        });
        // As Daemon::runningModeForContext answers: no engine runs on the
        // opt-out, so it snaps, and every other context here snaps too.
        m_wta->setContextModeResolver([](const QString&, int, const QString&) {
            return PhosphorZones::AssignmentEntry::Snapping;
        });
        QVERIFY(floatOnA(w));
        placeOnDesktop(w, 2);
        optOutOnB(2);
        QVERIFY(unplugA());
        OutputSettleRow row = unplugRow(w);
        row.placeableNow = false;

        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));

        const auto key = m_snapEngine->heldKeyForWindow(w);
        QVERIFY(key.has_value());
        QCOMPARE(key->desktop, 2);
        QVERIFY(!m_snapEngine->isFloating(w));
        QVERIFY(m_snapEngine->hasParked(w, kA));
    }

    // A float left waiting for its arrival, because its context could not be
    // told when A went away, is owed nothing on the opt-out, and the bit the
    // park kept in snap goes with it. Mutation: drop the opt-out test in
    // adoptPendingEvacueeFloat and the arrival is reported floated.
    void aPendingSnapFloatArrivingOnTheOptOutIsNotOwed()
    {
        const QString w = QStringLiteral("app|pending-opt-out");
        const auto clear = qScopeGuard([this] {
            m_wta->service()->setEngineFloatResolver({});
            m_snapEngine->setContextModeResolver({});
        });
        QVERIFY(pendSnapFloatOnB(w));
        m_layoutManager->setCurrentActivity(QStringLiteral("act-2"));
        optOutOnB(1);

        QVERIFY(!m_wta->adoptPendingEvacueeFloat(w, kB));

        QVERIFY(!m_snapEngine->isFloating(w));
        QVERIFY(!m_wta->service()->isWindowFloating(w));
    }

    // A window announced on the output it is parked for, after an effect
    // reload lost the records that would have classified it, goes back into
    // its parked place.
    void anAnnounceOnTheParkedOutputReseats()
    {
        const QString w = QStringLiteral("app|reannounced");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QStringList heard;
        QObject listener;
        connect(m_wta, &WindowTrackingAdaptor::parkReseated, &listener, [&heard](const QString& id, const QString&) {
            heard.append(QStringLiteral("reseated:") + id);
        });
        connect(m_wta, &WindowTrackingAdaptor::parkDropped, &listener, [&heard](const QString& id, const QString&) {
            heard.append(QStringLiteral("dropped:") + id);
        });
        QVERIFY(m_wta->readoptOnArrival(w, kA));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[0]);
        // The re-seat is said ahead of the drop (F1147).
        QCOMPARE(heard, (QStringList{QStringLiteral("reseated:") + w, QStringLiteral("dropped:") + w}));
        QVERIFY(!m_wta->readoptOnArrival(w, kA));
    }

    // An arrival whose context runs another mode now seats nothing: its park
    // still goes, but no re-seat is said, so the effect leaves the window to
    // its open (F1147).
    void anAnnounceThatSeatsNothingSaysNoReseat()
    {
        const QString w = QStringLiteral("app|reannounced-elsewhere");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_snapEngine->setContextModeResolver([](const QString&, int, const QString&) {
            return PhosphorZones::AssignmentEntry::Autotile;
        });
        const auto clear = qScopeGuard([this] {
            m_snapEngine->setContextModeResolver({});
        });
        QSignalSpy reseated(m_wta, &WindowTrackingAdaptor::parkReseated);
        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        QVERIFY(!m_wta->readoptOnArrival(w, kA));
        QVERIFY(m_wta->service()->zoneForWindow(w).isEmpty());
        QCOMPARE(dropped.count(), 1);
        QVERIFY2(reseated.isEmpty(), "nothing was put back");
    }

    // A seat that leaves another context parked on that output (another
    // engine still holds a place there) is said all the same, and the park
    // stays: parkReseated alone means seated and still parked (F1147).
    void aSeatThatLeavesAContextParkedSaysTheReseatAlone()
    {
        const QString w = QStringLiteral("app|reseated-still-parked");
        QVERIFY(snapOnA(w));
        QVERIFY(unplugA());
        StillParkedEngine holder;
        holder.physicalId = kA;
        m_wta->setEngines(m_snapEngine, &holder, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QSignalSpy reseated(m_wta, &WindowTrackingAdaptor::parkReseated);
        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        QVERIFY(m_wta->readoptOnArrival(w, kA));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[0]);
        QCOMPARE(reseated.count(), 1);
        QCOMPARE(reseated.first().at(0).toString(), w);
        QVERIFY2(dropped.isEmpty(), "a context is still parked on A");
    }

    // A settle row that names an output gone before the daemon's own
    // screen-removed handler ran retires it through the same primitive.
    void aSettleNamingAnOutputGoneRunsTheRetirer()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(kA, QRect(0, 0, 1920, 1080), kA);
        fake.addScreen(kB, QRect(1920, 0, 1920, 1080), kB);
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        QStringList retired;
        wta->setOutputRetirer([&retired](const QString& id) {
            retired.append(id);
        });
        wta->reportOutputSettle({unplugRow(QStringLiteral("app|first"))});
        QCOMPARE(retired, QStringList{kA});
    }

private:
    /// A tiling engine that keeps a context of every window parked for
    /// @p physicalId and seats nothing itself.
    struct StillParkedEngine : StubPlacementEngine
    {
        QString physicalId;
        bool hasParked(const QString&, const QString& physicalScreenId) const override
        {
            return physicalScreenId.isEmpty() || physicalScreenId == physicalId;
        }
    };

    // The helpers answer whether they did what they name, for the row to
    // QVERIFY: a QVERIFY inside one would return from the helper only.

    [[nodiscard]] bool snapOnA(const QString& windowId)
    {
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], kA, 1);
        return m_wta->service()->zoneForWindow(windowId) == m_zoneIds[0];
    }

    /// Snap @p windowId on A and float it there, with the daemon's float
    /// resolver (the snap engine's answer), so the service reads the float
    /// from the engine's stores as it does in the daemon. The caller clears
    /// it, with a guard armed before the call.
    [[nodiscard]] bool floatOnA(const QString& windowId)
    {
        if (!snapOnA(windowId)) {
            return false;
        }
        m_wta->service()->setEngineFloatResolver([this](const QString& id) {
            return m_snapEngine->isFloating(id);
        });
        m_snapEngine->setWindowFloat(windowId, true, kA);
        return m_snapEngine->isFloating(windowId) && m_wta->service()->isWindowFloating(windowId);
    }

    /// @p windowId snap-floated on A, on activity act-2 while act-1 is in view,
    /// and evacuated minimized onto B: its float waits for its arrival there,
    /// which the rows' adopts read. The caller clears the float resolver.
    [[nodiscard]] bool pendSnapFloatOnB(const QString& windowId)
    {
        installRegistry();
        PhosphorEngine::WindowMetadata meta;
        meta.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        meta.activity = QStringLiteral("act-2");
        m_registry->upsert(PhosphorIdentity::WindowId::extractInstanceId(windowId), meta);
        m_registry->canonicalizeWindowId(windowId);
        if (!floatOnA(windowId) || !unplugA()) {
            return false;
        }
        m_layoutManager->setCurrentActivity(QStringLiteral("act-1"));
        OutputSettleRow row = unplugRow(windowId);
        row.placeableNow = false;
        return verdictOf(m_wta->reportOutputSettle({row}), windowId) == int(OutputSettleVerdict::EvacueeFloat);
    }

    /// Put @p windowId on @p desktop in the window registry (desktop 1 is in view).
    void placeOnDesktop(const QString& windowId, int desktop)
    {
        installRegistry();
        PhosphorEngine::WindowMetadata meta;
        meta.appId = PhosphorIdentity::WindowId::extractAppId(windowId);
        meta.virtualDesktop = desktop;
        m_registry->upsert(PhosphorIdentity::WindowId::extractInstanceId(windowId), meta);
        m_registry->canonicalizeWindowId(windowId);
    }

    /// @p windowId snap-floated on A and evacuated onto B while on desktop 2,
    /// which tiles on B, with desktop 1 (snapping) in view: its float waits
    /// for its arrival there, unless a context-mode resolver says desktop 2
    /// runs snapping. The caller clears the float resolver.
    [[nodiscard]] bool pendOnTilingDesktopOfB(const QString& windowId)
    {
        if (!floatOnA(windowId)) {
            return false;
        }
        placeOnDesktop(windowId, 2);
        PhosphorZones::AssignmentEntry tiled;
        tiled.mode = PhosphorZones::AssignmentEntry::Autotile;
        tiled.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(kB, 2, QString(), tiled);
        if (!unplugA()) {
            return false;
        }
        OutputSettleRow row = unplugRow(windowId);
        row.placeableNow = false;
        return verdictOf(m_wta->reportOutputSettle({row}), windowId) == int(OutputSettleVerdict::EvacueeFloat);
    }

    /// Give desktop @p desktop of B the tiling opt-out ("autotile:none"), and
    /// route snap's context modes as Daemon::routedModeForContext does: the
    /// opt-out answers Autotile, any other context here snaps. The caller
    /// clears snap's resolver, with a guard armed before the call.
    void optOutOnB(int desktop)
    {
        PhosphorZones::AssignmentEntry optOut;
        optOut.mode = PhosphorZones::AssignmentEntry::Autotile;
        optOut.tilingAlgorithm = QString(PhosphorZones::NoTilingAlgorithm);
        m_layoutManager->setAssignmentEntryDirect(kB, desktop, QString(), optOut);
        m_snapEngine->setContextModeResolver([this](const QString& screen, int d, const QString& activity) {
            const PhosphorZones::AssignmentEntry entry = m_layoutManager->assignmentEntryForScreen(screen, d, activity);
            return entry.mode == PhosphorZones::AssignmentEntry::Autotile
                    && entry.tilingAlgorithm == PhosphorZones::NoTilingAlgorithm
                ? PhosphorZones::AssignmentEntry::Autotile
                : PhosphorZones::AssignmentEntry::Snapping;
        });
    }

    /// The daemon's retire for A: park, prune, release the parked slots.
    /// Answers whether the park took anything.
    [[nodiscard]] bool unplugA()
    {
        const bool parked = !m_wta->parkOutput(kA).isEmpty();
        m_snapEngine->pruneStatesForRemovedScreen(kA);
        m_wta->releaseParkedSlots(kA);
        return parked;
    }
};

QTEST_MAIN(TestWtaEvacuees)
#include "test_wta_evacuees.moc"

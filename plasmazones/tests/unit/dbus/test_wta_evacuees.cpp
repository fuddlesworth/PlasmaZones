// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_evacuees.cpp
 * @brief The daemon's evacuee ledger: what reportOutputSettle answers for a
 *        window KWin moved off an output that went away, and when that output
 *        comes back. A window returned untouched goes back into its zone; one
 *        the user touched, or that closed, is not re-seated; a PlasmaZones
 *        placement while the output was away is re-asserted.
 */

#include "wta_convenience_fixture.h"

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
        snapOnA(w);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));
        unplugA();
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
        snapOnA(w);
        unplugA();
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
        snapOnA(w);
        unplugA();
        m_wta->reportOutputSettle({unplugRow(w)});

        m_snapEngine->commitSnap(w, m_zoneIds[1], kB);
        QVERIFY(!m_snapEngine->hasParked(w, QString()));

        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({redockRow(w)}), w), int(OutputSettleVerdict::Reassert));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[1]);
    }

    // A window that closes while A is away has nothing left to return.
    void closeDropsThePark()
    {
        const QString w = QStringLiteral("app|closed");
        snapOnA(w);
        unplugA();
        QSignalSpy dropped(m_wta, &WindowTrackingAdaptor::parkDropped);
        m_wta->windowClosed(w, 0, kB);
        QVERIFY(!m_snapEngine->hasParked(w, QString()));
        QCOMPARE(dropped.count(), 1);
    }

    // An evacuee that cannot float where KWin put it (minimized, or out of
    // view) is adopted floating at its first announce instead of tiled, once.
    void anEvacueeOutOfViewFloatsAtItsFirstAnnounce()
    {
        const QString w = QStringLiteral("app|minimized-evacuee");
        OutputSettleRow row = unplugRow(w);
        row.placeableNow = false;
        QCOMPARE(verdictOf(m_wta->reportOutputSettle({row}), w), int(OutputSettleVerdict::EvacueeFloat));
        QVERIFY(m_wta->takeEvacueeFloatPending(w, kB));
        QVERIFY(!m_wta->takeEvacueeFloatPending(w, kB));
    }

    // A window announced on the output it is parked for, after an effect
    // reload lost the records that would have classified it, goes back into
    // its parked place.
    void anAnnounceOnTheParkedOutputReseats()
    {
        const QString w = QStringLiteral("app|reannounced");
        snapOnA(w);
        unplugA();
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        QVERIFY(m_wta->readoptOnArrival(w, kA));
        QCOMPARE(m_wta->service()->zoneForWindow(w), m_zoneIds[0]);
        QVERIFY(!m_wta->readoptOnArrival(w, kA));
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
    void snapOnA(const QString& windowId)
    {
        m_snapEngine->setCurrentDesktopForScreen(kA, 1);
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], kA, 1);
        QCOMPARE(m_wta->service()->zoneForWindow(windowId), m_zoneIds[0]);
    }

    /// The daemon's retire for A: park, prune, release the parked slots.
    void unplugA()
    {
        QVERIFY(!m_wta->parkOutput(kA).isEmpty());
        m_snapEngine->pruneStatesForRemovedScreen(kA);
        m_wta->releaseParkedSlots(kA);
    }
};

QTEST_MAIN(TestWtaEvacuees)
#include "test_wta_evacuees.moc"

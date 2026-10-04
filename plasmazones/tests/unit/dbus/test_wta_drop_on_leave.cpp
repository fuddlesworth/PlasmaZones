// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_drop_on_leave.cpp
 * @brief WindowTrackingAdaptor::releaseLeftScreens, the one primitive every
 *        cross-screen move runs: what each engine still holds of a window on
 *        a monitor it left is dropped, the arriving engine is left alone, and
 *        a record naming an output that went away stays parked. Plus the
 *        sites that call it on a daemon-driven move (the engine relays and
 *        the cross-mode handoff).
 */

#include "wta_convenience_fixture.h"
#include "helpers/StubPlacementEngine.h"
#include "helpers/WindowPlacementBuilders.h"

#include <QScopeGuard>

class TestWtaDropOnLeave : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private:
    /// Snap @p windowId into a zone on desktops 1 and 2 of m_screenId, and
    /// record its placement.
    void snapOnTwoDesktops(const QString& windowId, const QString& screenId)
    {
        m_snapEngine->setCurrentDesktopForScreen(screenId, 1);
        m_wta->service()->assignWindowToZone(windowId, m_zoneIds[0], screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(screenId, 2);
        m_snapEngine->stateForWindowOnScreen(windowId, screenId, 2)
            ->assignWindowToZone(windowId, m_zoneIds[1], screenId, 2);
        m_snapEngine->setCurrentDesktopForScreen(screenId, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(windowId));
    }

    QString snapSlotState(const QString& windowId) const
    {
        const auto rec = m_wta->service()->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state : QString();
    }

private Q_SLOTS:
    void init()
    {
        initFixture();
        installPerScreenResolver();
    }
    void cleanup()
    {
        m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        cleanupFixture();
    }

    // Every snap membership on the monitor left goes, on every desktop, with
    // the record's snap slot, and silently: a zone-clear relay would read as
    // an untile to autotile. The other tiling engine is asked to let go off
    // the kept screen; the arriving one is not.
    void releaseLeftScreens_dropsOffMonitorSnapMembershipsSilently()
    {
        StubPlacementEngine arriving;
        StubPlacementEngine other;
        m_wta->setEngines(m_snapEngine, &arriving, &other);
        const QString w = QStringLiteral("app|leaves");
        const QString dest = QStringLiteral("DP-2");
        snapOnTwoDesktops(w, m_screenId);
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));

        QSignalSpy zoneSpy(m_wta->service(), &PhosphorPlacement::WindowTrackingService::windowZoneChanged);
        m_wta->releaseLeftScreens(w, dest, &arriving);

        QVERIFY2(!m_snapEngine->isWindowTracked(w), "no snap membership may stay on the monitor left");
        QCOMPARE(zoneSpy.count(), 0);
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QCOMPARE(other.releasedOffScreen.size(), 1);
        QCOMPARE(other.releasedOffScreen.first(), qMakePair(w, dest));
        QVERIFY2(arriving.releasedOffScreen.isEmpty(), "the arriving engine may still be moving the window");
    }

    // Snap arriving on the new screen keeps the slot: the arrival is the
    // placement it now describes. The memberships on the monitor left go all
    // the same.
    void releaseLeftScreens_snapArrivalKeepsTheSlot()
    {
        const QString w = QStringLiteral("app|snap-arrives");
        snapOnTwoDesktops(w, m_screenId);

        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), m_snapEngine);

        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    // A crossing between virtual screens of one monitor drops the membership
    // on the virtual screen left but keeps the record's slot.
    void releaseLeftScreens_virtualScreenCrossingKeepsTheSlot()
    {
        const QString w = QStringLiteral("app|vs-leave");
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        const QString vs1 = QStringLiteral("DP-1/vs:1");
        m_snapEngine->setCurrentDesktopForScreen(vs0, 1);
        m_snapEngine->setCurrentDesktopForScreen(vs1, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], vs0, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));

        m_wta->releaseLeftScreens(w, vs1, nullptr);

        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    // A float bit dropped by the release is closed out on the float channel
    // once, where it was broadcast: a second release has nothing to say.
    void releaseLeftScreens_closesOutABroadcastFloatOnce()
    {
        const QString w = QStringLiteral("app|float-close-out");
        QVERIFY(m_wta->relayWindowFloatingChanged(w, true, m_screenId));
        QVERIFY(!m_wta->service()->isWindowFloating(w));

        QSignalSpy floatSpy(m_wta, &WindowTrackingAdaptor::windowFloatingChanged);
        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), nullptr);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.first().at(1).toBool(), false);
        QCOMPARE(floatSpy.first().at(2).toString(), QStringLiteral("DP-2"));

        m_wta->releaseLeftScreens(w, QStringLiteral("DP-2"), nullptr);
        QCOMPARE(floatSpy.count(), 1);
    }

    // A record naming an output that is no longer connected is the evacuee
    // park's: a move on the remaining output leaves its slot alone. The same
    // record on a connected output is released.
    void releaseLeftScreens_recordOnAnUnpluggedOutputStaysParked()
    {
        const QString dp1 = QStringLiteral("DP-1");
        const QString dp2 = QStringLiteral("DP-2");
        const QString gone = QStringLiteral("HDMI-9");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(dp1, QRect(0, 0, 1920, 1080), dp1);
        fake.addScreen(dp2, QRect(1920, 0, 1920, 1080), dp2);
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        QVERIFY(screenMgr.physicalScreenFor(dp1).isValid());
        QVERIFY(!screenMgr.physicalScreenFor(gone).isValid());
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        QCOMPARE(wta->service()->screenManager(), &screenMgr);
        const auto recordOn = [wta](const QString& windowId, const QString& screenId) {
            wta->service()->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
                windowId, QStringLiteral("app"), PhosphorEngine::WindowPlacement::stateSnapped(),
                PhosphorEngine::WindowPlacement::snapEngineId(), screenId));
        };
        const auto slotState = [wta](const QString& windowId) {
            return wta->service()
                ->placementStore()
                .peekExact(windowId)
                ->slotFor(PhosphorEngine::WindowPlacement::snapEngineId())
                .state;
        };
        const QString parked = QStringLiteral("app|parked-record");
        const QString left = QStringLiteral("app|left-record");
        recordOn(parked, gone);
        recordOn(left, dp1);

        wta->releaseLeftScreens(parked, dp2, nullptr);
        wta->releaseLeftScreens(left, dp2, nullptr);

        QCOMPARE(slotState(parked), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        QCOMPARE(slotState(left), QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }

    // An engine relays its move marker BEFORE it re-keys the window: the
    // screens left are released first, and the relaying engine is never asked
    // to let go of the window it is moving.
    void engineRelay_leavesBeforeTheMarker()
    {
        StubPlacementEngine autotile;
        m_wta->setEngines(m_snapEngine, &autotile, nullptr);
        const QString w = QStringLiteral("app|relayed");
        const QString dest = QStringLiteral("DP-2");
        snapOnTwoDesktops(w, m_screenId);

        QSignalSpy markerSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        Q_EMIT autotile.windowOutputMoveExpected(w, dest);

        QCOMPARE(markerSpy.count(), 1);
        QCOMPARE(markerSpy.first().at(0).toString(), w);
        QCOMPARE(markerSpy.first().at(1).toString(), dest);
        QVERIFY(!m_snapEngine->isWindowTracked(w));
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QVERIFY2(autotile.releasedOffScreen.isEmpty(), "the relaying engine must not release its own move");
    }

    // A keyboard move from snapping onto a tiling monitor leaves nothing on
    // the monitor left: the snap release keeps another desktop's membership
    // whose context is still snapping, which on the SAME monitor is right,
    // but across monitors would drag the window back on that desktop (F60).
    void crossModeMove_snapToTilingLeavesNothingOnTheSourceMonitor()
    {
        StubPlacementEngine scroll;
        m_wta->setEngines(m_snapEngine, nullptr, &scroll);
        const QString dest = QStringLiteral("DP-2");
        PhosphorZones::AssignmentEntry scrolling;
        scrolling.mode = PhosphorZones::AssignmentEntry::Scrolling;
        m_layoutManager->setAssignmentEntryDirect(dest, 0, QString(), scrolling);
        m_layoutManager->assignLayout(m_screenId, 0, QString(), m_testLayout);
        const QString w = QStringLiteral("app|crossmode-leave");
        snapOnTwoDesktops(w, m_screenId);

        QSignalSpy markerSpy(m_wta, &WindowTrackingAdaptor::windowOutputMoveExpected);
        Q_EMIT m_snapEngine->crossModeMoveRequested(w, dest, 0, QStringLiteral("right"));

        QCOMPARE(scroll.received.size(), 1);
        QCOMPARE(markerSpy.count(), 1);
        QVERIFY2(!m_snapEngine->isWindowTracked(w), "the other desktop's zone on the monitor left must go too");
        QCOMPARE(snapSlotState(w), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        QVERIFY2(scroll.releasedOffScreen.isEmpty(), "the arriving engine must not be released");
    }
};

QTEST_MAIN(TestWtaDropOnLeave)
#include "test_wta_drop_on_leave.moc"

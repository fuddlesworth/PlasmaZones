// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

/**
 * @brief The reopen contract on the snap engine: a window is restored only on
 *        the screen it opens on.
 *
 * A window whose OWN record names another screen left that screen while
 * nothing tracked it (a move while the daemon was down, a restart after a
 * move). It is not restored, and its engine slots are released so no engine
 * reads them as a home later. Another instance's record on another monitor is
 * left alone for an instance that opens there.
 */
class TestSnapEngineReopenContract : public SnapEngineTestFixture
{
    Q_OBJECT

    void recordSnapped(const QString& windowId, const QString& screenId)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = QStringLiteral("app");
        rec.screenId = screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(m_wts->placementStore().record(rec));
    }

    QString snapSlotState(const QString& windowId) const
    {
        const auto rec = m_wts->placementStore().peekExact(windowId);
        return rec ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state : QString();
    }

private Q_SLOTS:

    void testOwnRecordOnAnotherScreen_isReleasedNotRestored()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // The window was snapped on DP-1 and is announced, same uuid and
        // untracked, on DP-2: it was moved while nothing tracked it.
        recordSnapped(QStringLiteral("app|w"), QStringLiteral("DP-1"));
        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(engine, QStringLiteral("app|w"), QStringLiteral("DP-2"), &result);

        QVERIFY2(!result.shouldSnap, "a window that left its screen is not restored");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("left its recorded screen")),
                 "the leave branch must be the one that refused the restore");
        QCOMPARE(snapSlotState(QStringLiteral("app|w")), QString(PhosphorEngine::WindowPlacement::stateReleased()));
        m_wts->setSnapState(nullptr);
    }

    void testOwnRecordOnTheOpeningScreen_isNotALeave()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        recordSnapped(QStringLiteral("app|w"), QStringLiteral("DP-1"));
        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(engine, QStringLiteral("app|w"), QStringLiteral("DP-1"), &result);

        QVERIFY2(captureIsNonEmpty(lines), "snap-engine log capture produced nothing");
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("left its recorded screen")),
                 "a window on its own recorded screen has not left it");
        QCOMPARE(snapSlotState(QStringLiteral("app|w")), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        m_wts->setSnapState(nullptr);
    }

    void testSiblingRecordOnAnotherScreen_isLeftAlone()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // Another instance of the app was snapped on DP-1; a fresh instance
        // opens on DP-2. It is not moved there, and the record stays snapped
        // for an instance that does open on DP-1.
        recordSnapped(QStringLiteral("app|old"), QStringLiteral("DP-1"));
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-2"), &result);

        QVERIFY2(captureIsNonEmpty(lines), "snap-engine log capture produced nothing");
        QVERIFY2(!result.shouldSnap, "another monitor's snapped record must not pull the window there");
        QCOMPARE(snapSlotState(QStringLiteral("app|old")), QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        m_wts->setSnapState(nullptr);
    }

    // F626: a re-entry of the window's OWN floated record (any reason but
    // Open) leaves the window where the user put it. Only a genuine open moves
    // it to the recorded position.
    void testReEntry_keepsFloatPosition_data()
    {
        QTest::addColumn<int>("reason");
        QTest::addColumn<int>("moves");
        QTest::newRow("open") << static_cast<int>(PhosphorEngine::RestoreReason::Open) << 1;
        QTest::newRow("restart sweep") << static_cast<int>(PhosphorEngine::RestoreReason::DaemonRestartSweep) << 0;
        QTest::newRow("pending sweep") << static_cast<int>(PhosphorEngine::RestoreReason::PendingSweep) << 0;
        QTest::newRow("desktop arrival") << static_cast<int>(PhosphorEngine::RestoreReason::DesktopArrival) << 0;
    }

    void testReEntry_keepsFloatPosition()
    {
        QFETCH(int, reason);
        QFETCH(int, moves);
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|w");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(60, 40, 1024, 768));
        QVERIFY(m_wts->placementStore().record(rec));

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        engine.resolveWindowRestore(QStringLiteral("app|w"), QStringLiteral("DP-1"), /*sticky*/ false,
                                    PhosphorEngine::WindowKind::Unknown,
                                    static_cast<PhosphorEngine::RestoreReason>(reason));
        QCOMPARE(geoSpy.count(), moves);
        m_wts->setSnapState(nullptr);
    }

    // F626: the managed-restore gate (restoreWindowsToZonesOnLogin off) skips
    // a genuine open's snapped record, but not a re-entry of the window's own
    // record: that window is already in the zone.
    void testReEntry_ignoresManagedRestoreGate_data()
    {
        QTest::addColumn<int>("reason");
        QTest::addColumn<bool>("gated");
        QTest::newRow("open") << static_cast<int>(PhosphorEngine::RestoreReason::Open) << true;
        QTest::newRow("restart sweep") << static_cast<int>(PhosphorEngine::RestoreReason::DaemonRestartSweep) << false;
        QTest::newRow("unminimize") << static_cast<int>(PhosphorEngine::RestoreReason::Unminimize) << false;
    }

    void testReEntry_ignoresManagedRestoreGate()
    {
        QFETCH(int, reason);
        QFETCH(bool, gated);
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setManagedRestorePredicate([](const QString&) {
            return false;
        });
        recordSnapped(QStringLiteral("app|w"), QStringLiteral("DP-1"));

        beginLogCapture();
        engine.resolveWindowRestore(QStringLiteral("app|w"), QStringLiteral("DP-1"), /*sticky*/ false,
                                    PhosphorEngine::WindowKind::Unknown,
                                    static_cast<PhosphorEngine::RestoreReason>(reason));
        const QStringList lines = endLogCapture();
        QVERIFY2(captureIsNonEmpty(lines), "snap-engine log capture produced nothing");
        QCOMPARE(lines.join(QLatin1Char('\n')).contains(QStringLiteral("managed-restore gate skipped snapped record")),
                 gated);
        m_wts->setSnapState(nullptr);
    }
};

QTEST_MAIN(TestSnapEngineReopenContract)
#include "test_snap_engine_reopen_contract.moc"

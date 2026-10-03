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
};

QTEST_MAIN(TestSnapEngineReopenContract)
#include "test_snap_engine_reopen_contract.moc"

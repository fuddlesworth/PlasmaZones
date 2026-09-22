// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

/**
 * @brief A remembered zone is only put back when it belongs to the layout the
 *        restore context runs now (discussion #1104).
 *
 * Zone ids are unique across layouts, so a zone remembered under layout A
 * still resolves geometry when the desktop has since been given layout B, and
 * the window came back sitting in a "ghost" layout. The snapped-restore arm
 * has to ask the registry which layout the (screen, desktop, activity) context
 * runs and refuse a zone that is not in it.
 */
class TestSnapRestoreLayoutContext : public SnapEngineTestFixture
{
    Q_OBJECT

private:
    static const inline QString kScreen = QStringLiteral("DP-1");
    static const inline QString kStaleLine = QStringLiteral("are not in the layout for desktop");

    PhosphorZones::Layout* addLayout(int zoneCount)
    {
        PhosphorZones::Layout* layout = PlasmaZones::createTestLayout(zoneCount, m_layoutManager);
        m_layoutManager->addLayout(layout);
        return layout;
    }

    void recordSnapped(const QString& zoneId)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = kScreen;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{zoneId};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);
    }

private Q_SLOTS:
    // The window was snapped into layout A's zone; the screen now runs
    // layout B. The remembered zone must not be re-applied.
    void staleZoneFromAnotherLayoutIsNotRestored()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        PhosphorZones::Layout* rememberedIn = addLayout(1);
        PhosphorZones::Layout* runningNow = addLayout(2);
        m_layoutManager->assignLayout(kScreen, 0, QString(), runningNow);
        recordSnapped(rememberedIn->zones().first()->id().toString());

        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(engine, QStringLiteral("app|new"), kScreen, &result);

        QVERIFY(captureIsNonEmpty(lines));
        QVERIFY2(!result.shouldSnap, "a zone from a layout the screen no longer runs must not be re-applied");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(kStaleLine),
                 "the layout-context gate must be the branch that declined the snapped record");
        m_wts->setSnapState(nullptr);
    }

    // Control: the remembered zone is in the layout the screen runs, so the
    // gate stays out of the way (geometry cannot resolve in a guiless
    // fixture, so the branch is identified by the absence of the gate's log).
    void zoneInTheRunningLayoutPassesTheGate()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        PhosphorZones::Layout* runningNow = addLayout(2);
        m_layoutManager->assignLayout(kScreen, 0, QString(), runningNow);
        recordSnapped(runningNow->zones().first()->id().toString());

        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(engine, QStringLiteral("app|new"), kScreen, &result);

        QVERIFY(captureIsNonEmpty(lines));
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(kStaleLine),
                 "a zone in the running layout must not trip the layout-context gate");
        m_wts->setSnapState(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestSnapRestoreLayoutContext)
#include "test_snap_restore_layout_context.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

#include <PhosphorIdentity/WindowId.h>

#include <QSet>

/**
 * @brief Discussion #1106: a window the open path leaves floating gets its
 *        remembered free SIZE back, position untouched. KDE apps save their
 *        window size to their own config on every resize, and a snap is a
 *        resize, so an app with a snapped window opens its next window at the
 *        zone's size. The size comes from the window's own record, else from
 *        a live sibling's.
 */
class TestSnapEngineFreeSize : public SnapEngineTestFixture
{
    Q_OBJECT

    static PhosphorEngine::WindowPlacement snappedRecord(const QString& windowId, const QString& freeScreen,
                                                         const QRect& freeGeo)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = windowId;
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(freeScreen, freeGeo);
        return rec;
    }

    void setLiveProbe(QSet<QString>& liveInstances)
    {
        m_wts->placementStore().setLiveInstanceProbe([&liveInstances](const QString& windowId) {
            return liveInstances.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
    }

    void activateLayout()
    {
        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);
    }

private Q_SLOTS:
    // A second instance opened beside a SNAPPED sibling: no record of its own
    // to consume (the sibling's is live-bound), so it floats and gets the
    // sibling's free SIZE. Position is the compositor's: no
    // geometryRestoreRequested, whatever the restore-position predicate says.
    void testNewSiblingOfSnappedWindow_getsFreeSizeNotPosition()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });
        QSet<QString> liveInstances{QStringLiteral("first")};
        setLiveProbe(liveInstances);
        activateLayout();

        const QRect siblingFree(300, 200, 893, 663);
        m_wts->placementStore().record(snappedRecord(QStringLiteral("app|first"), QStringLiteral("DP-1"), siblingFree));

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|second"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "the sibling's zone is not the second instance's");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|first")),
                 "the live sibling keeps its record (not consumed and re-bound)");
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        const QList<QVariant> args = sizeSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("app|second"));
        QCOMPARE(args.at(1).toSize(), siblingFree.size());
        QCOMPARE(args.at(2).toString(), QStringLiteral("DP-1"));
        m_wts->setSnapState(nullptr);
    }

    // The sibling's free geometry was captured on another monitor: a size
    // from a different output says nothing about this one, so nothing is
    // applied. And once the sibling closes there is no source at all.
    void testNewSibling_noFreeSizeFromOtherScreenOrClosedSibling()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        QSet<QString> liveInstances{QStringLiteral("first")};
        setLiveProbe(liveInstances);
        activateLayout();

        m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), QStringLiteral("DP-2"), QRect(10, 10, 800, 600)));

        QSignalSpy sizeSpy(&engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        (void)engine.resolveWindowRestore(QStringLiteral("app|second"), QStringLiteral("DP-1"), /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 0);

        // The sibling closes: its record is now reopen memory for the NEXT
        // open to consume, not a window to inherit from.
        liveInstances.clear();
        (void)engine.resolveWindowRestore(QStringLiteral("app|third"), QStringLiteral("DP-1"), /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    // The window's OWN record is the first source: a reopen whose snapped
    // record the managed-restore gate declined (restore to zone off) is left
    // floating at the app-saved zone size, and gets its own free size back.
    void testDeclinedSnappedRecord_restoresOwnFreeSize()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setManagedRestorePredicate([](const QString&) {
            return false;
        });
        activateLayout();

        const QRect ownFree(120, 80, 1000, 700);
        m_wts->placementStore().record(snappedRecord(QStringLiteral("app|orig"), QStringLiteral("DP-1"), ownFree));

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        const QList<QVariant> args = sizeSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("app|new"));
        QCOMPARE(args.at(1).toSize(), ownFree.size());
        m_wts->setSnapState(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestSnapEngineFreeSize)
#include "test_snap_engine_free_size.moc"

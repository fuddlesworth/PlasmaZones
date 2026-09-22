// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"
#include "helpers/WindowPlacementBuilders.h"

#include <PhosphorScreens/Manager.h>
#include "FakeScreenProvider.h"

#include <QSet>

/**
 * @brief Discussion #1106: a window the open path leaves floating gets its
 *        remembered free SIZE back, position untouched. KDE apps save their
 *        window size to their own config on every resize, and a snap is a
 *        resize, so an app with a snapped window opens its next window at the
 *        zone's size. The size comes from the window's own record, else from
 *        the earliest-recorded live sibling's, only for a first placement,
 *        only from a rect on the opening screen, clamped to that screen.
 */
class TestSnapEngineFreeSize : public SnapEngineTestFixture
{
    Q_OBJECT

    static const inline QString kScreen = QStringLiteral("DP-1");

    // Owned by the fixture, not the slot: the probe captures it by reference
    // and lives in the store until cleanup() deletes the service.
    QSet<QString> m_liveInstances;

    static PhosphorEngine::WindowPlacement snappedRecord(const QString& windowId, const QString& zoneId,
                                                         const QString& freeScreen, const QRect& freeGeo)
    {
        auto rec = PlasmaZones::TestHelpers::makePlacement(windowId, QStringLiteral("app"),
                                                           PhosphorEngine::WindowPlacement::stateSnapped(),
                                                           PhosphorEngine::WindowPlacement::snapEngineId(), kScreen);
        rec.engines[PhosphorEngine::WindowPlacement::snapEngineId()].zoneIds = QStringList{zoneId};
        rec.freeGeometryByScreen.insert(freeScreen, freeGeo);
        return rec;
    }

    void setLiveProbe(PhosphorPlacement::WindowTrackingService* wts)
    {
        wts->placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(m_liveInstances));
    }

    /// The layout the screen runs, so a remembered zone from it passes the
    /// #1104 layout gate and the gate under test is the one that decides.
    PhosphorZones::Layout* activateLayout()
    {
        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);
        m_layoutManager->assignLayout(kScreen, 0, QString(), layout);
        return layout;
    }

    static QString firstZoneId(PhosphorZones::Layout* layout)
    {
        return layout->zones().first()->id().toString();
    }

    /// An engine over the fixture's service; detached on every exit, including
    /// a mid-slot QVERIFY abort, so cleanup() never meets a dangling state.
    struct EngineOn
    {
        SnapEngine engine;
        PhosphorPlacement::WindowTrackingService* wts;
        EngineOn(PhosphorPlacement::WindowTrackingService* service, PhosphorZones::LayoutRegistry* layouts,
                 StubSettings* settings)
            : engine(layouts, service, nullptr, nullptr, nullptr)
            , wts(service)
        {
            engine.setEngineSettings(settings);
            wts->setSnapState(engine.snapState());
        }
        ~EngineOn()
        {
            wts->setSnapState(nullptr);
        }
    };

private Q_SLOTS:
    void init()
    {
        SnapEngineTestFixture::init();
        m_liveInstances.clear();
    }

    // A second instance opened beside a SNAPPED sibling: no record of its own
    // to consume (the sibling's is live-bound), so it floats and gets the
    // sibling's free SIZE. Position is the compositor's: no
    // geometryRestoreRequested even with the position restore opted in,
    // because the sibling's spot is not this window's.
    void testNewSiblingOfSnappedWindow_getsFreeSizeNotPosition()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        on.engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });
        m_liveInstances.insert(QStringLiteral("first"));
        setLiveProbe(m_wts);
        auto* layout = activateLayout();

        const QRect siblingFree(300, 200, 893, 663);
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, siblingFree)));

        QSignalSpy floatSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        const PhosphorEngine::SnapResult result =
            on.engine.resolveWindowRestore(QStringLiteral("app|second"), kScreen, /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "the sibling's zone is not the second instance's");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|first")),
                 "the live sibling keeps its record (not consumed and re-bound)");
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        const QList<QVariant> args = sizeSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("app|second"));
        QCOMPARE(args.at(1).toSize(), siblingFree.size());
        QCOMPARE(args.at(2).toString(), kScreen);

        // Once floated, every re-drive of the same window returns at the
        // already-floating guard: the size is restored exactly once.
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|second"), kScreen, /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 0);
        QCOMPARE(floatSpy.count(), 1);
    }

    // The sibling's free geometry is keyed to another screen: a size from a
    // different output says nothing about this one, so nothing is applied. A
    // closed sibling's record is a different thing: it is reopen memory that
    // the opener CONSUMES and re-binds as its own, so the size then comes from
    // the own-record source.
    void testNewSibling_otherScreenKeyRefused_closedSiblingBecomesOwnRecord()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        m_liveInstances.insert(QStringLiteral("first"));
        setLiveProbe(m_wts);
        auto* layout = activateLayout();

        QVERIFY(m_wts->placementStore().record(snappedRecord(QStringLiteral("app|first"), firstZoneId(layout),
                                                             QStringLiteral("DP-2"), QRect(10, 10, 800, 600))));

        QSignalSpy floatSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        const PhosphorEngine::SnapResult second =
            on.engine.resolveWindowRestore(QStringLiteral("app|second"), kScreen, /*sticky*/ false);
        QVERIFY(!second.shouldSnap);
        QCOMPARE(floatSpy.count(), 1); // the float terminal ran, so the size path was consulted
        QCOMPARE(sizeSpy.count(), 0);

        // The sibling closes and had a DP-1 rect after all: the next open
        // consumes its record, re-binds it, and reads the size as its own.
        m_liveInstances.clear();
        QVERIFY(m_wts->placementStore().take(QStringLiteral("app|first"), QStringLiteral("app")).has_value());
        const QRect closedFree(20, 20, 640, 480);
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, closedFree)));
        // Snapped record declined by the managed gate, so the opener floats.
        on.engine.setManagedRestorePredicate([](const QString&) {
            return false;
        });
        const PhosphorEngine::SnapResult third =
            on.engine.resolveWindowRestore(QStringLiteral("app|third"), kScreen, /*sticky*/ false);
        QVERIFY(!third.shouldSnap);
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|first")), "the closed record was consumed");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|third")), "and re-bound to the opener");
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), closedFree.size());
    }

    // The window's OWN record is the first source and beats a live sibling's:
    // a reopen whose snapped record the managed-restore gate declined (restore
    // to zone off) is left floating at the app-saved zone size, and gets its
    // own free size back, not the sibling's.
    void testDeclinedSnappedRecord_restoresOwnFreeSizeOverSibling()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        on.engine.setManagedRestorePredicate([](const QString&) {
            return false;
        });
        m_liveInstances.insert(QStringLiteral("sib"));
        setLiveProbe(m_wts);
        auto* layout = activateLayout();

        const QRect ownFree(120, 80, 1000, 700);
        const QRect siblingFree(0, 0, 500, 400);
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|sib"), firstZoneId(layout), kScreen, siblingFree)));
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|orig"), firstZoneId(layout), kScreen, ownFree)));

        QSignalSpy geoSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(on.engine, QStringLiteral("app|new"), kScreen, &result);

        QVERIFY(captureIsNonEmpty(lines));
        QVERIFY(!result.shouldSnap);
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("managed-restore gate skipped")),
                 "the managed gate, not the layout gate, must be the branch that declined the record");
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        const QList<QVariant> args = sizeSpy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("app|new"));
        QCOMPARE(args.at(1).toSize(), ownFree.size());
    }

    // A matched Float rule floats the window before the chain, and that
    // terminal restores the size too.
    void testFloatByRule_restoresSiblingFreeSize()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        on.engine.setFloatPredicate([](const QString&, const QString&) {
            return true;
        });
        m_liveInstances.insert(QStringLiteral("first"));
        setLiveProbe(m_wts);
        auto* layout = activateLayout();
        const QRect siblingFree(300, 200, 700, 500);
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, siblingFree)));

        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(on.engine, QStringLiteral("app|second"), kScreen, &result);
        QVERIFY(captureIsNonEmpty(lines));
        QVERIFY(lines.join(QLatin1Char('\n')).contains(QStringLiteral("floated by rule")));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), siblingFree.size());
    }

    // A floated record restored WITHOUT a move (position restore off) leaves
    // the window where KWin put it, at the app-saved size: that branch gives
    // the size back too, from the record just re-bound.
    void testFloatedRecordWithoutMove_restoresOwnFreeSize()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        on.engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });
        activateLayout();
        const QRect ownFree(40, 40, 900, 650);
        QVERIFY(m_wts->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
            QStringLiteral("app|orig"), QStringLiteral("app"), PhosphorEngine::WindowPlacement::stateFloating(),
            PhosphorEngine::WindowPlacement::snapEngineId(), kScreen, ownFree)));

        QSignalSpy geoSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        const PhosphorEngine::SnapResult result =
            on.engine.resolveWindowRestore(QStringLiteral("app|new"), kScreen, /*sticky*/ false);
        QVERIFY(!result.shouldSnap);
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), ownFree.size());

        // This branch runs ahead of the already-floating guard, so a
        // re-resolve of the same window lands here again: the size must not
        // be restored twice at a window the user may have sized since.
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|new"), kScreen, /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 0);

        // With the move opted in the full rect restores and no size-only
        // request follows: the two never double-apply. The first window is
        // live now, so the fresh one consumes the newly recorded record.
        m_liveInstances.insert(QStringLiteral("new"));
        setLiveProbe(m_wts);
        on.engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });
        QVERIFY(m_wts->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
            QStringLiteral("app|orig2"), QStringLiteral("app"), PhosphorEngine::WindowPlacement::stateFloating(),
            PhosphorEngine::WindowPlacement::snapEngineId(), kScreen, ownFree)));
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|new2"), kScreen, /*sticky*/ false);
        QCOMPARE(geoSpy.count(), 1);
        QCOMPARE(sizeSpy.count(), 0);
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new")), "the live first window keeps its record");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|orig2")),
                 "the closed record was the one consumed");
    }

    // Only a first placement may resize. The daemon-restart sweep and the
    // unminimize re-drive act on a window the user is looking at.
    void testReResolveOfVisibleWindow_neverResizes()
    {
        EngineOn on(m_wts, m_layoutManager, m_settings);
        m_liveInstances.insert(QStringLiteral("first"));
        setLiveProbe(m_wts);
        auto* layout = activateLayout();
        QVERIFY(m_wts->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, QRect(0, 0, 800, 600))));

        QSignalSpy floatSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|restart"), kScreen, /*sticky*/ false,
                                             PhosphorEngine::WindowKind::Unknown,
                                             PhosphorEngine::RestoreReason::DaemonRestartSweep);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(sizeSpy.count(), 0);
        on.engine.applyNoMatchFloatDefault(QStringLiteral("app|unmin"), kScreen,
                                           PhosphorEngine::RestoreReason::Unminimize);
        QCOMPARE(floatSpy.count(), 2);
        QCOMPARE(sizeSpy.count(), 0);
        // The parked-open continuation is a first placement.
        on.engine.applyNoMatchFloatDefault(QStringLiteral("app|arrival"), kScreen,
                                           PhosphorEngine::RestoreReason::DesktopArrival);
        QCOMPARE(sizeSpy.count(), 1);
    }

    // The screen-containment gate and the clamp need real output geometry
    // behind the service (the guiless fixture's null ScreenManager fails
    // open), so this builds its own service over a FakeScreenProvider.
    void testScreenGate_refusesRectOffScreenAndClampsToAvailable()
    {
        PhosphorScreens::FakeScreenProvider provider;
        provider.addScreen(kScreen, QRect(0, 0, 1920, 1080));
        provider.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080));
        PhosphorScreens::ScreenManager manager(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &provider, .useGeometrySensors = false});
        manager.start();
        auto svc = std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager, &manager, nullptr);
        EngineOn on(svc.get(), m_layoutManager, m_settings);
        m_liveInstances.insert(QStringLiteral("first"));
        setLiveProbe(svc.get());
        auto* layout = activateLayout();

        // Keyed to DP-1 but lying on DP-2: refused by the containment gate,
        // which the per-screen key alone cannot catch.
        QVERIFY(svc->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, QRect(2000, 100, 800, 600))));
        QSignalSpy floatSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy sizeSpy(&on.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|second"), kScreen, /*sticky*/ false);
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(sizeSpy.count(), 0);

        // A rect captured at a larger resolution overlaps DP-1, so it passes
        // the gate, and its size is clamped to what the screen can show.
        QVERIFY(svc->placementStore().record(
            snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen, QRect(0, 0, 2500, 1500))));
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|third"), kScreen, /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 1);
        // The fake provider has no QScreen, so the available area is the
        // screen's whole geometry.
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(1920, 1080));

        // A free rect of exactly a zone's size is a snapped window's spawn
        // frame, not a free life: refused as a source, whichever sibling
        // holds it. The other sibling's real free rect is used instead, even
        // though it was recorded later.
        const QRect zoneRect = svc->zoneGeometry(firstZoneId(layout), kScreen);
        QVERIFY2(zoneRect.isValid(), "the zone must resolve on the fake screen for this case to mean anything");
        QVERIFY(svc->placementStore().record(snappedRecord(QStringLiteral("app|first"), firstZoneId(layout), kScreen,
                                                           QRect(QPoint(40, 40), zoneRect.size()))));
        m_liveInstances.insert(QStringLiteral("later"));
        const QRect laterFree(60, 60, 700, 500);
        QVERIFY(svc->placementStore().record(
            snappedRecord(QStringLiteral("app|later"), firstZoneId(layout), kScreen, laterFree)));
        (void)on.engine.resolveWindowRestore(QStringLiteral("app|fourth"), kScreen, /*sticky*/ false);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), laterFree.size());
        // The fixture-owned set outlives this local service, so no probe reset
        // is needed before `svc` dies.
    }
};

QTEST_GUILESS_MAIN(TestSnapEngineFreeSize)
#include "test_snap_engine_free_size.moc"

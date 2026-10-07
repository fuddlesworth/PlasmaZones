// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_capture_guards.cpp
 * @brief Capture-orchestrator guards in WindowTrackingAdaptor::captureWindowPlacement:
 *        the managed-frame float-back refusal and the untracked-window
 *        no-engine contract (frozen per-mode snap slot preserved).
 *
 * Unlike test_wta_convenience's shared fixture, these tests wire a REAL
 * ScreenManager (FakePhysicalScreenSource): the untracked-window regression only
 * reproduces when the orchestrator can resolve a screen for the live frame —
 * with a null ScreenManager the fabricated slot has no geometry and
 * hasRestorableContent() drops it before it can clobber the record, making the
 * assertion vacuous.
 */

#include <QTest>
#include <QCoreApplication>
#include <QRect>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <memory>

#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include "FakePhysicalScreenSource.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "helpers/AutotileTestHelpers.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubPlacementEngine.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// The autotile-side stand-in: only lastManagedRect() matters here, standing in
// for AutotileEngine's last-applied tile rect memory in the capture guards.
using StubTileRectEngine = StubPlacementEngine;

class TestWtaCaptureGuards : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    PlasmaZones::StubZoneDetector* m_zoneDetector = nullptr;

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new PlasmaZones::StubZoneDetector(nullptr);
    }

    void cleanup()
    {
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    // Regression (float-restores-onto-its-own-tile): on a float-from-tiled
    // toggle the autotile engine clears its tiled bit BEFORE the compositor
    // repositions the window, so when captureWindowPlacement runs (from
    // setWindowFloating) the live frame still IS the tile rect and the
    // isWindowEngineTiled gate no longer refuses it. The capture must
    // compare against the engine's remembered last-applied rect and skip the
    // free-geometry write, or applyGeometryForFloat (which runs AFTER the
    // capture) reads the poisoned value back and "restores" the window onto
    // its own tile — permanently overwriting the genuine float-back.
    void testScrollArmRefusesStillTiledFrameAsFloatBack()
    {
        // The managed-frame refusal reads BOTH tiling engines' last managed
        // rect: a strip rect must never be adopted as float-back geometry
        // either. Every other test in this file wires the stub into the
        // AUTOTILE slot and passes nullptr for scroll, so dropping the scroll
        // arm would fail nothing else. This is the same scenario as
        // testRefusesStillTiledFrameAsFloatBack with the stub in the scroll slot.
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        // Declared BEFORE `parent` (and therefore before wta) so the stub
        // outlives the adaptor on EVERY exit path — an early QVERIFY failure
        // skips the explicit setEngines detach at the tail, and while the
        // adaptor's engine ref is a self-nulling QPointer, outliving it makes
        // the teardown safety structural rather than incidental. Same reason
        // the sibling tests spell this out.
        StubTileRectEngine scrollEngine;
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        // Autotile slot deliberately empty: only the SCROLL arm may answer here.
        wta->setEngines(snap.get(), nullptr, &scrollEngine);

        const QString windowId = QStringLiteral("ghostty|inst-strip");
        const QString screenId = QStringLiteral("DP-1");
        const QString appId = wta->service()->currentAppIdFor(windowId);
        const QRect stripRect(1540, 8, 1524, 829);
        const QRect realFreeBack(1743, 795, 800, 628);

        wta->service()->recordFreeGeometry(windowId, screenId, realFreeBack, true);

        snap->snapState()->setFloatingOnScreen(windowId, screenId, 0);
        wta->setFrameGeometry(windowId, stripRect.x(), stripRect.y(), stripRect.width(), stripRect.height());
        scrollEngine.managedRect = stripRect;

        wta->captureWindowPlacement(windowId);

        // The strip rect must be refused: the genuine float-back survives.
        auto rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), realFreeBack);

        // Positive control: once the frame leaves the strip rect the capture
        // adopts it, so the refusal above is a real discrimination rather than
        // the capture never writing anything.
        const QRect movedFrame(600, 400, 900, 700);
        wta->setFrameGeometry(windowId, movedFrame.x(), movedFrame.y(), movedFrame.width(), movedFrame.height());
        wta->captureWindowPlacement(windowId);

        rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), movedFrame);

        wta->setEngines(snap.get(), nullptr, nullptr);
    }

    void testRefusesStillTiledFrameAsFloatBack()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        // Declared BEFORE parent (and therefore before wta) so the stub
        // outlives the adaptor on EVERY exit path — an early QVERIFY failure
        // skips the explicit setEngines detach below, and while the adaptor's
        // engine ref is a self-nulling QPointer, outliving it makes the
        // teardown safety structural rather than incidental.
        StubTileRectEngine tileEngine;
        // Declared BEFORE `parent` so the engine outlives the WTA/service on
        // EVERY exit path — an early QVERIFY return skips the tail cleanup,
        // and reverse destruction would otherwise tear the SnapEngine down
        // first while the service's snap resolver still points into it.
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), &tileEngine, nullptr);

        const QString windowId = QStringLiteral("ghostty|inst-tile");
        const QString screenId = QStringLiteral("DP-1");
        const QString appId = wta->service()->currentAppIdFor(windowId);
        const QRect tileRect(1540, 8, 1524, 829);
        const QRect realFreeBack(1743, 795, 800, 628);

        // The window's genuine float-back from an earlier session.
        wta->service()->recordFreeGeometry(windowId, screenId, realFreeBack, true);

        // Post-flip state: snap-side reports the window floating on DP-1 (the
        // capture's owning-engine slot), the live frame still sits on the tile
        // rect, and the autotile engine remembers having applied exactly it.
        snap->snapState()->setFloatingOnScreen(windowId, screenId, 0);
        wta->setFrameGeometry(windowId, tileRect.x(), tileRect.y(), tileRect.width(), tileRect.height());
        tileEngine.managedRect = tileRect;

        wta->captureWindowPlacement(windowId);

        auto rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), realFreeBack);

        // Once the frame moves off the tile rect (the user repositioned the
        // floating window), the capture adopts the genuine free frame.
        const QRect movedFrame(600, 400, 900, 700);
        wta->setFrameGeometry(windowId, movedFrame.x(), movedFrame.y(), movedFrame.width(), movedFrame.height());
        wta->captureWindowPlacement(windowId);

        rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), movedFrame);

        wta->setEngines(snap.get(), nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
    }

    // Regression (phantom snap-float after a mode round-trip): a snapped
    // window adopted by autotile is handoff-released from snap, so snap
    // tracks NOTHING for it — its record's snap slot is the frozen per-mode
    // memory windowsReleased restores from on return to snapping. A capture
    // running in that window (windowsReleased's own setWindowFloating(false)
    // fires one) used to fabricate a "floating" snap slot for the untracked
    // window; with a resolvable screen for the live frame the fabricated slot
    // gains geometry, survives hasRestorableContent(), and the record merge
    // overwrites the frozen snapped slot — which windowsReleased then read
    // back and "restored" as a float the user never made. The capture must
    // leave the record untouched instead.
    void testUntrackedWindowPreservesFrozenSnapSlot()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        // Engine before parent — see testMinimizedCapturePreservesPreMinimizePlacement.
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, nullptr);

        const QString windowId = QStringLiteral("app|handoff");
        const QString appId = wta->service()->currentAppIdFor(windowId);
        const QString zoneId = QUuid::createUuid().toString();

        // Frozen per-mode memory: the record's snap slot says snapped.
        PhosphorEngine::WindowPlacement p;
        p.windowId = windowId;
        p.appId = appId;
        p.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        slot.zoneIds = QStringList{zoneId};
        p.engines.insert(snap->engineId(), slot);
        QVERIFY(wta->service()->placementStore().record(p));

        // Snap does not track the window (post-handoffRelease state), but the
        // effect still reports a live frame for it — one whose centre resolves
        // to DP-1, so a fabricated slot WOULD gain geometry and clobber the
        // record if the untracked-capture guard were missing.
        QVERIFY(!snap->isWindowTracked(windowId));
        wta->setFrameGeometry(windowId, 619, 514, 1380, 907);

        wta->captureWindowPlacement(windowId);

        const auto rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        const PhosphorEngine::EngineSlot after = rec->slotFor(snap->engineId());
        QCOMPARE(after.state, QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        QCOMPARE(after.zoneIds, QStringList{zoneId});

        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        wta->setEngines(nullptr, nullptr, nullptr); // symmetric with the siblings; the QPointer self-null is incidental
        snap.reset();
    }

    // Minimize-float is a live occupancy suspension, not a placement change.
    // Every capture source must preserve the pre-minimize snap slot while the
    // compositor reports the window hidden.
    void testMinimizedCapturePreservesPreMinimizePlacement()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorEngine::WindowRegistry registry;
        // Engine declared BEFORE parent so reverse destruction tears the
        // parent (and the WTA/service inside it) down first — destroying the
        // SnapEngine while the service's snap-state resolver still points into
        // it would deref freed state. (The other tests cross-reference this
        // rationale.)
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, nullptr);

        const QString instanceId = QStringLiteral("minimized-instance");
        const QString windowId = QStringLiteral("app|minimized-instance");
        const QString screenId = QStringLiteral("DP-1");
        const QString zoneId = QUuid::createUuid().toString();

        PhosphorEngine::WindowMetadata metadata;
        metadata.appId = QStringLiteral("app");
        metadata.isMinimized = true;
        registry.upsert(instanceId, metadata);

        PhosphorEngine::WindowPlacement placement;
        placement.windowId = windowId;
        placement.appId = metadata.appId;
        placement.screenId = screenId;
        placement.virtualDesktop = 4;
        placement.activity = QStringLiteral("activity-a");
        placement.freeGeometryByScreen.insert(screenId, QRect(140, 100, 1000, 720));
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        slot.zoneIds = QStringList{zoneId};
        placement.engines.insert(snap->engineId(), slot);
        PhosphorEngine::EngineSlot autotileSlot;
        autotileSlot.state = QString(PhosphorEngine::WindowPlacement::stateTiled());
        autotileSlot.order = 2;
        placement.engines.insert(PhosphorEngine::WindowPlacement::autotileEngineId(), autotileSlot);
        QVERIFY(wta->service()->placementStore().record(placement));
        const auto before = wta->service()->placementStore().peekExact(windowId);
        QVERIFY(before);

        // Runtime post-minimize state is floating and carries a valid frame.
        // Without the minimized guard, capture overwrites the frozen snapped
        // slot with this temporary float.
        snap->snapState()->setFloatingOnScreen(windowId, screenId, 1);
        wta->setFrameGeometry(windowId, 620, 410, 1100, 760);
        wta->captureWindowPlacement(windowId);

        auto stored = wta->service()->placementStore().peekExact(windowId);
        QVERIFY(stored);
        QVERIFY(stored->sameContentAs(*before));
        QCOMPARE(stored->sequence, before->sequence);

        // Once visible, the same capture is allowed and proves the test's
        // floating setup would have changed the record without the guard.
        metadata.isMinimized = false;
        registry.upsert(instanceId, metadata);
        wta->captureWindowPlacement(windowId);
        stored = wta->service()->placementStore().peekExact(windowId);
        QVERIFY(stored);
        QCOMPARE(stored->slotFor(snap->engineId()).state, QString(PhosphorEngine::WindowPlacement::stateFloating()));

        wta->setEngines(snap.get(), nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
        wta->setWindowRegistry(nullptr);
    }

    void testTilingCloseRelayCapturesEngineSlotBeforeUntrack()
    {
        // The close relay's ordering contract: TilingAdaptor::windowClosed
        // runs the shared capture funnel BEFORE forwarding the close to the
        // engine, so the persisted slot carries the LIVE order — obtainable
        // only pre-untrack (the engine's capturePlacement answers nullopt the
        // moment it drops the window).
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        PhosphorTileEngine::AutotileEngine autotile(nullptr, wta->service(), nullptr,
                                                    PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("DP-1");
        autotile.setAutotileScreens({screen});
        wta->setEngines(nullptr, &autotile, nullptr);

        auto* tiling = new TilingAdaptor(&screenMgr, &parent);
        tiling->setWindowTrackingAdaptor(wta);
        tiling->setLifecycleEngines(QVector<PhosphorEngine::IPlacementEngine*>{&autotile});

        autotile.windowOpened(QStringLiteral("aa|a1"), screen);
        autotile.windowOpened(QStringLiteral("bb|b1"), screen);
        QCoreApplication::processEvents();

        tiling->windowClosed(QStringLiteral("bb|b1"));
        // The engine no longer tracks it, and the record carries the LIVE
        // order (1) the pre-untrack capture read.
        QVERIFY(!autotile.isWindowTracked(QStringLiteral("bb|b1")));
        const auto rec = wta->service()->placementStore().peekExact(QStringLiteral("bb|b1"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(autotile.engineId()).state, QString(PhosphorEngine::WindowPlacement::stateTiled()));
        QCOMPARE(rec->slotFor(autotile.engineId()).order, 1);

        tiling->clearEngine();
        wta->setEngines(nullptr, nullptr, nullptr);
    }

    void testMinimizedClosePreserveSynthesizesOwningEngineSlotForPureFloatRecord()
    {
        // The minimize-preserve's synthesized-slot branch, with a WIRED
        // ModeEngineIdResolver and a PURE-FLOAT record (geometry, no engine
        // slots): the preserved record must gain the owning engine's floating
        // slot — under the old snap-hardcoded key (or the engines.isEmpty()
        // gate with a foreign slot present) the reopening tiling engine saw
        // no verdict and re-tiled a window that was floating pre-minimize.
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorEngine::WindowRegistry registry;
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, nullptr);
        wta->service()->setModeEngineIdResolver([](const QString&, const QString&) {
            return QString(PhosphorEngine::WindowPlacement::scrollingEngineId());
        });

        const QString instanceId = QStringLiteral("min-purefloat-instance");
        const QString windowId = QStringLiteral("app|min-purefloat-instance");
        const QString screenId = QStringLiteral("DP-1");

        PhosphorEngine::WindowMetadata metadata;
        metadata.appId = QStringLiteral("app");
        metadata.isMinimized = true;
        registry.upsert(instanceId, metadata);

        PhosphorEngine::WindowPlacement placement;
        placement.windowId = windowId;
        placement.appId = metadata.appId;
        placement.screenId = screenId;
        placement.freeGeometryByScreen.insert(screenId, QRect(140, 100, 1000, 720));
        QVERIFY(wta->service()->placementStore().record(placement));

        // CLOSE-form capture (authoritative screen supplied) while minimized:
        // the preserve branch runs and must synthesize the scrolling slot.
        wta->captureWindowPlacement(windowId, screenId);
        const auto stored = wta->service()->placementStore().peekExact(windowId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY(!stored->engines.contains(QString(PhosphorEngine::WindowPlacement::snapEngineId())));
        QCOMPARE(stored->freeGeometryFor(screenId), QRect(140, 100, 1000, 720));

        wta->service()->setModeEngineIdResolver({});
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
        wta->setWindowRegistry(nullptr);
    }

    void testMinimizedCloseOnAnotherScreenForgetsItsDesktopZones()
    {
        // The minimize preserve downgrades a slot recorded on DP-2 when the
        // window closes on DP-1; the stored record keeps none of DP-2's
        // per-desktop zones, which the store's merge used to put back (F282).
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorEngine::WindowRegistry registry;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);

        const QString instanceId = QStringLiteral("min-cross-instance");
        const QString windowId = QStringLiteral("app|min-cross-instance");
        PhosphorEngine::WindowMetadata metadata;
        metadata.appId = QStringLiteral("app");
        metadata.isMinimized = true;
        registry.upsert(instanceId, metadata);

        PhosphorEngine::WindowPlacement placement;
        placement.windowId = windowId;
        placement.appId = metadata.appId;
        placement.screenId = QStringLiteral("DP-2");
        placement.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(140, 100, 1000, 720));
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        slot.zoneIds = QStringList{QUuid::createUuid().toString()};
        slot.zonesByDesktop.insert(1, slot.zoneIds);
        slot.zonesByDesktop.insert(2, QStringList{QUuid::createUuid().toString()});
        placement.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(wta->service()->placementStore().record(placement));

        wta->captureWindowPlacement(windowId, QStringLiteral("DP-1"));

        const auto stored = wta->service()->placementStore().peekExact(windowId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->screenId, QStringLiteral("DP-1"));
        const PhosphorEngine::EngineSlot snapSlot = stored->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        QCOMPARE(snapSlot.state, QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY(snapSlot.zonesByDesktop.isEmpty());

        wta->setWindowRegistry(nullptr);
    }

    void testMinimizedCloseRebindsChangedAppPrefixWithoutLosingPlacement()
    {
        // A minimized window closing under a MUTATED appId prefix must re-key its
        // durable record to the current windowId instead of stranding it under
        // the old prefix (where a reopen under the new class could never find it).
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorEngine::WindowRegistry registry;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);

        const QString instanceId = QStringLiteral("renamed-minimized-instance");
        const QString oldWindowId = QStringLiteral("oldclass|renamed-minimized-instance");
        const QString currentWindowId = QStringLiteral("newclass|renamed-minimized-instance");
        const QString screenId = QStringLiteral("DP-1");
        PhosphorEngine::WindowMetadata metadata;
        metadata.appId = QStringLiteral("newclass");
        metadata.virtualDesktop = 3;
        metadata.activity = QStringLiteral("activity-a");
        metadata.isMinimized = true;
        registry.upsert(instanceId, metadata);

        PhosphorEngine::WindowPlacement placement;
        placement.windowId = oldWindowId;
        placement.appId = QStringLiteral("oldclass");
        placement.screenId = screenId;
        placement.virtualDesktop = 1;
        placement.activity = QStringLiteral("old-activity");
        placement.freeGeometryByScreen.insert(screenId, QRect(100, 120, 900, 700));
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        slot.zoneIds = QStringList{QUuid::createUuid().toString()};
        placement.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(wta->service()->placementStore().record(placement));

        wta->captureWindowPlacement(currentWindowId, screenId);

        QCOMPARE(wta->service()->placementStore().size(), 1);
        const auto stored = wta->service()->placementStore().peekExact(currentWindowId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->windowId, currentWindowId);
        QCOMPARE(stored->appId, QStringLiteral("newclass"));
        QCOMPARE(stored->virtualDesktop, 3);
        QCOMPARE(stored->activity, QStringLiteral("activity-a"));
        QCOMPARE(stored->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()), slot);
        QCOMPARE(stored->freeGeometryFor(screenId), placement.freeGeometryFor(screenId));

        wta->setWindowRegistry(nullptr);
    }

    // Control for the rebind test above: the SAME capture with the registry
    // reporting NOT-minimized must not take the minimize preserve branch.
    // With no engine wired and no reported frame the capture has nothing to
    // record, so the record stays untouched under its OLD id and context —
    // proving the rebind + context refresh asserted above came specifically
    // from the minimize branch, not from some always-on path.
    void testNotMinimizedCloseLeavesRecordUntouched()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        PhosphorEngine::WindowRegistry registry;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);

        const QString instanceId = QStringLiteral("renamed-visible-instance");
        const QString oldWindowId = QStringLiteral("oldclass|renamed-visible-instance");
        const QString currentWindowId = QStringLiteral("newclass|renamed-visible-instance");
        const QString screenId = QStringLiteral("DP-1");
        PhosphorEngine::WindowMetadata metadata;
        metadata.appId = QStringLiteral("newclass");
        metadata.virtualDesktop = 3;
        metadata.activity = QStringLiteral("activity-a");
        metadata.isMinimized = false;
        registry.upsert(instanceId, metadata);

        PhosphorEngine::WindowPlacement placement;
        placement.windowId = oldWindowId;
        placement.appId = QStringLiteral("oldclass");
        placement.screenId = screenId;
        placement.virtualDesktop = 1;
        placement.activity = QStringLiteral("old-activity");
        placement.freeGeometryByScreen.insert(screenId, QRect(100, 120, 900, 700));
        PhosphorEngine::EngineSlot slot;
        slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        slot.zoneIds = QStringList{QUuid::createUuid().toString()};
        placement.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        QVERIFY(wta->service()->placementStore().record(placement));

        wta->captureWindowPlacement(currentWindowId, screenId);

        QCOMPARE(wta->service()->placementStore().size(), 1);
        // peekExact matches by instance id, so it finds the record either way;
        // the discriminating assertions are the UNCHANGED id and context.
        const auto stored = wta->service()->placementStore().peekExact(currentWindowId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->windowId, oldWindowId);
        QCOMPARE(stored->virtualDesktop, 1);
        QCOMPARE(stored->activity, QStringLiteral("old-activity"));

        wta->setWindowRegistry(nullptr);
    }

    // The engine-miss CLOSE fallback carries the same tile-rect poison guard
    // as the primary capture path: a window tiled by autotile, handed off
    // (the tiled bit clears; the engine's last-applied rect memory
    // deliberately survives), and closed before ever being repositioned
    // still sits on its tile rect — recording that via recordFloatingClose
    // would restore the reopened window onto the tile, not a free spot. A
    // close frame OFF the tile rect records normally.
    void testClosePathRefusesStillTiledFrame()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        // Same structural stub-before-parent ordering as the primary-path test.
        StubTileRectEngine tileEngine;
        // Engine before parent — see testMinimizedCapturePreservesPreMinimizePlacement.
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), &tileEngine, nullptr);

        const QString windowId = QStringLiteral("app|closed-on-tile");
        const QString screenId = QStringLiteral("DP-1");
        const QString appId = wta->service()->currentAppIdFor(windowId);
        const QRect tileRect(1540, 8, 1524, 829);

        // Seed a genuine free-geometry record first: the guard must
        // PRESERVE it, and without the seed the assert below would only
        // prove "did not create", passing even if the guard clobbered an
        // existing record with the tile rect.
        const QRect realFreeBack(120, 90, 800, 600);
        wta->service()->recordFreeGeometry(windowId, screenId, realFreeBack, true);

        // Untracked by snap (no float/zone state) and the stub tile engine's
        // capturePlacement default returns nullopt — the close capture takes
        // the engine-miss fallback. The frame still equals the remembered
        // tile rect, so the fallback must record NOTHING new.
        wta->setFrameGeometry(windowId, tileRect.x(), tileRect.y(), tileRect.width(), tileRect.height());
        tileEngine.managedRect = tileRect;
        wta->captureWindowPlacement(windowId, screenId);
        const auto seeded = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY2(seeded && seeded->freeGeometryFor(screenId) == realFreeBack,
                 "a close frame still on the tile rect must not clobber the recorded free geometry");

        // A close frame off the tile rect records the genuine free position.
        const QRect freeFrame(600, 400, 900, 700);
        wta->setFrameGeometry(windowId, freeFrame.x(), freeFrame.y(), freeFrame.width(), freeFrame.height());
        wta->captureWindowPlacement(windowId, screenId);
        const auto rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), freeFrame);

        wta->setEngines(snap.get(), nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
    }

    // Close DURING a fullscreen hold: the window's frame is the output rect.
    // Tiling.windowClosed drops the engine slot before WindowTracking's
    // capture runs, so without the engine's closed-hold memory the orphan
    // fallback saw an untracked window on a frame matching no remembered tile
    // rect and recorded the OUTPUT as its float-back.
    void testCloseDuringFullscreenHoldKeepsFreeGeometry()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        const QRect output(0, 0, 3072, 1728);
        fake.addScreen(QStringLiteral("DP-1"), output, QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject owner;
        auto* scroll = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr, &owner);
        scroll->setScreenGeometryProviders(
            [output](const QString&) {
                return output;
            },
            [output](const QString&) {
                return output;
            });
        scroll->setActiveScreens({QStringLiteral("DP-1")});
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, scroll);

        const QString a = QStringLiteral("app|a");
        const QString b = QStringLiteral("app|b");
        const QString screenId = QStringLiteral("DP-1");
        const QString appId = wta->service()->currentAppIdFor(b);
        scroll->windowOpened(a, screenId, 0, 0);
        scroll->windowOpened(b, screenId, 0, 0);
        scroll->windowFocused(b, screenId);
        QCoreApplication::processEvents();

        // The genuine free size, which the close must PRESERVE.
        const QRect realFreeBack(120, 90, 800, 600);
        wta->service()->recordFreeGeometry(b, screenId, realFreeBack, true);

        QVERIFY(scroll->setWindowFullscreenFloat(b, true, screenId));
        QVERIFY(scroll->isFullscreenFloated(b));
        // KWin's fullscreen frame.
        wta->setFrameGeometry(b, output.x(), output.y(), output.width(), output.height());

        // Effect relay order: Tiling.windowClosed first (the engine untracks),
        // WindowTracking.windowClosed second (the capture with a screen).
        scroll->windowClosed(b);
        QVERIFY2(!scroll->isWindowTracked(b), "the engine must have dropped the slot");
        wta->windowClosed(b, 0, screenId);

        const auto rec = wta->service()->placementStore().peek(b, appId);
        QVERIFY(rec);
        QVERIFY2(rec->freeGeometryFor(screenId) != output,
                 "a close during the hold must not record the output rect as the float-back");
        QCOMPARE(rec->freeGeometryFor(screenId), realFreeBack);
        // The memory is consumed by the capture, not left behind for good.
        QVERIFY2(!scroll->isFullscreenFloated(b), "WindowTracking.windowClosed must consume the closed-hold memory");

        wta->setEngines(snap.get(), nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
    }

    // F654: the first prune per daemon releases the engine slots of a live
    // window whose own record names another screen than its frame is on. A
    // window on its recorded screen keeps its slot, a window that fills its
    // output is compared at output level, and a later prune releases nothing.
    void testStartupLeaveSweep_releasesMovedWindowsSlots()
    {
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        fake.addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        PhosphorEngine::WindowRegistry registry;
        // Engine before parent, for the destruction order the tests above give.
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        wta->setWindowRegistry(&registry);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, nullptr);
        auto& store = wta->service()->placementStore();
        const auto recordSnapped = [&](const QString& windowId, const QString& screenId) {
            PhosphorEngine::WindowPlacement p;
            p.windowId = windowId;
            p.appId = QStringLiteral("app");
            p.screenId = screenId;
            PhosphorEngine::EngineSlot slot;
            slot.state = QString(PhosphorEngine::WindowPlacement::stateSnapped());
            slot.zoneIds = QStringList{QUuid::createUuid().toString()};
            p.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
            return store.record(p);
        };
        const auto snapState = [&](const QString& windowId) {
            const auto rec = store.peekExact(windowId);
            return rec ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state : QString();
        };
        const QString snapped = QString(PhosphorEngine::WindowPlacement::stateSnapped());
        const QString released = QString(PhosphorEngine::WindowPlacement::stateReleased());

        // Moved to DP-2 while no daemon ran.
        QVERIFY(recordSnapped(QStringLiteral("app|left"), QStringLiteral("DP-1")));
        wta->setFrameGeometry(QStringLiteral("app|left"), 2000, 100, 800, 600);
        // Still where it was recorded.
        QVERIFY(recordSnapped(QStringLiteral("app|stay"), QStringLiteral("DP-1")));
        wta->setFrameGeometry(QStringLiteral("app|stay"), 100, 100, 800, 600);
        // Recorded on another virtual screen of the same output, maximized:
        // its frame centre says nothing about which half it belongs to.
        QVERIFY(recordSnapped(QStringLiteral("app|full"), QStringLiteral("DP-1/vs:1")));
        wta->setFrameGeometry(QStringLiteral("app|full"), 0, 0, 1920, 1080);
        PhosphorEngine::WindowMetadata maximized;
        maximized.appId = QStringLiteral("app");
        maximized.isMaximized = true;
        registry.upsert(QStringLiteral("full"), maximized);
        // The same record shape for a window that does not fill its output.
        QVERIFY(recordSnapped(QStringLiteral("app|half"), QStringLiteral("DP-1/vs:1")));
        wta->setFrameGeometry(QStringLiteral("app|half"), 100, 100, 800, 600);

        const QStringList alive{QStringLiteral("app|left"), QStringLiteral("app|stay"), QStringLiteral("app|full"),
                                QStringLiteral("app|half")};
        wta->pruneStaleWindows(alive);
        QCOMPARE(snapState(QStringLiteral("app|left")), released);
        QCOMPARE(snapState(QStringLiteral("app|stay")), snapped);
        QCOMPARE(snapState(QStringLiteral("app|full")), snapped);
        QCOMPARE(snapState(QStringLiteral("app|half")), released);

        // Once per daemon: a window that moves later is the live handlers'.
        wta->setFrameGeometry(QStringLiteral("app|stay"), 2000, 100, 800, 600);
        wta->pruneStaleWindows(alive);
        QCOMPARE(snapState(QStringLiteral("app|stay")), snapped);

        wta->setEngines(nullptr, nullptr, nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
        wta->setWindowRegistry(nullptr);
    }

    // A tiling hold the window keeps on the screen it left (a background
    // desktop's tile of a multi-desktop window) is released on EVERY branch of
    // windowScreenChanged. Only the free-window branch did it, so a snapped or
    // floating window moved by KWin kept the hold, and returning to that
    // desktop pulled it back across monitors.
    void testScreenChangedReleasesTilingHoldsOnEveryBranch()
    {
        StubTileRectEngine tileEngine; // outlives the adaptor, see the tests above
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap->setEngineSettings(m_settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), &tileEngine, nullptr);
        const auto teardown = qScopeGuard([wta, &snap] {
            wta->setEngines(nullptr, nullptr, nullptr);
            wta->service()->setSnapState(nullptr);
            wta->service()->setSnapEngine(nullptr);
            snap.reset();
        });

        const QString snapped = QStringLiteral("app|snapped-move");
        const QString floating = QStringLiteral("app|floating-move");
        const QString zoneId = QUuid::createUuid().toString();
        snap->snapState()->assignWindowToZone(snapped, zoneId, QStringLiteral("DP-1"), 1);
        snap->snapState()->setFloatingOnScreen(floating, QStringLiteral("DP-1"), 1);
        QCOMPARE(wta->service()->zoneForWindow(snapped), zoneId);

        wta->windowScreenChanged(snapped, QStringLiteral("DP-2"));
        wta->windowScreenChanged(floating, QStringLiteral("DP-2"));

        QVERIFY2(tileEngine.releasedOffScreen.contains(qMakePair(snapped, QStringLiteral("DP-2"))),
                 "the snapped branch must release a tiling hold off the new screen");
        QVERIFY2(tileEngine.releasedOffScreen.contains(qMakePair(floating, QStringLiteral("DP-2"))),
                 "the floating branch must release a tiling hold off the new screen");
        QVERIFY(wta->service()->zoneForWindow(snapped).isEmpty());
    }

    // A scroll engine tracking the window only on another desktop does not
    // capture first: the snap placement in view is what the record keeps, or
    // a multi-desktop window snapped here loses its zone at the next restart
    // (F361). Held in view, scroll does capture first.
    void testBackgroundScrollHoldDoesNotCaptureFirst()
    {
        StubTileRectEngine scrollEngine; // outlives the adaptor, see the tests above
        scrollEngine.id = QString(PhosphorEngine::WindowPlacement::scrollingEngineId());
        scrollEngine.captureState = QString(PhosphorEngine::WindowPlacement::stateTiled());
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap->setEngineSettings(m_settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr, &scrollEngine);
        const auto teardown = qScopeGuard([wta, &snap] {
            wta->setEngines(nullptr, nullptr, nullptr);
            wta->service()->setSnapState(nullptr);
            wta->service()->setSnapEngine(nullptr);
            snap.reset();
        });

        const QString w = QStringLiteral("app|background-column");
        scrollEngine.trackedElsewhere.insert(w);
        snap->snapState()->assignWindowToZone(w, QUuid::createUuid().toString(), QStringLiteral("DP-1"), 1);

        wta->captureWindowPlacement(w);
        auto rec = wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateSnapped()));
        QVERIFY(rec->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state.isEmpty());

        scrollEngine.trackedElsewhere.clear();
        scrollEngine.heldScreen.insert(w, QStringLiteral("DP-1"));
        wta->captureWindowPlacement(w);
        rec = wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::scrollingEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateTiled()));
    }
};

QTEST_MAIN(TestWtaCaptureGuards)
#include "test_wta_capture_guards.moc"

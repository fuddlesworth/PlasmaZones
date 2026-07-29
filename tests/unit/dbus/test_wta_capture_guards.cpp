// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_capture_guards.cpp
 * @brief Capture-orchestrator guards in WindowTrackingAdaptor::captureWindowPlacement:
 *        the stillOnTileRect float-back poison guard and the untracked-window
 *        no-engine contract (frozen per-mode snap slot preserved).
 *
 * Unlike test_wta_convenience's shared fixture, these tests wire a REAL
 * ScreenManager (FakeScreenProvider): the untracked-window regression only
 * reproduces when the orchestrator can resolve a screen for the live frame —
 * with a null ScreenManager the fabricated slot has no geometry and
 * hasRestorableContent() drops it before it can clobber the record, making the
 * assertion vacuous.
 */

#include <QTest>
#include <QRect>
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
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "FakeScreenProvider.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub autotile-side engine: only lastManagedRect() matters — it stands in
// for AutotileEngine's last-applied tile rect memory in the capture guard
// tests below. Everything else is a no-op.
// =========================================================================

class StubTileRectEngine : public PhosphorEngine::PlacementEngineBase
{
    Q_OBJECT
public:
    explicit StubTileRectEngine(QObject* parent = nullptr)
        : PhosphorEngine::PlacementEngineBase(parent)
    {
    }

    QRect managedRect; // returned for every window

    QRect lastManagedRect(const QString&) const override
    {
        return managedRect;
    }

    bool isActiveOnScreen(const QString&) const override
    {
        return false;
    }
    void windowOpened(const QString&, const QString&, int, int) override
    {
    }
    void windowClosed(const QString&) override
    {
    }
    void windowFocused(const QString&, const QString&) override
    {
    }
    void toggleWindowFloat(const QString&, const QString&) override
    {
    }
    void setWindowFloat(const QString&, bool, const QString&) override
    {
    }
    void focusInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void swapFocusedInDirection(const QString&, const PhosphorEngine::NavigationContext&) override
    {
    }
    void moveFocusedToPosition(int, const PhosphorEngine::NavigationContext&) override
    {
    }
    void rotateWindows(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void reapplyLayout(const PhosphorEngine::NavigationContext&) override
    {
    }
    void snapAllWindows(const PhosphorEngine::NavigationContext&) override
    {
    }
    void cycleFocus(bool, const PhosphorEngine::NavigationContext&) override
    {
    }
    void pushToEmptyZone(const PhosphorEngine::NavigationContext&) override
    {
    }
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext&) override
    {
    }
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext&) override
    {
    }
    void saveState() override
    {
    }
    void loadState() override
    {
    }
    PhosphorEngine::IPlacementState* stateForScreen(const QString&) override
    {
        return nullptr;
    }
    const PhosphorEngine::IPlacementState* stateForScreen(const QString&) const override
    {
        return nullptr;
    }
};

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
    // isWindowAutotileTiled gate no longer refuses it. The capture must
    // compare against the engine's remembered last-applied rect and skip the
    // free-geometry write, or applyGeometryForFloat (which runs AFTER the
    // capture) reads the poisoned value back and "restores" the window onto
    // its own tile — permanently overwriting the genuine float-back.
    void testRefusesStillTiledFrameAsFloatBack()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
        wta->setEngines(snap.get(), &tileEngine);

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

        wta->setEngines(snap.get(), nullptr);
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
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        // Engine before parent — see testMinimizedCapturePreservesPreMinimizePlacement.
        std::unique_ptr<SnapEngine> snap;
        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        snap = std::make_unique<SnapEngine>(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->setEngines(snap.get(), nullptr);

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
        snap.reset();
    }

    // Minimize-float is a live occupancy suspension, not a placement change.
    // Every capture source must preserve the pre-minimize snap slot while the
    // compositor reports the window hidden.
    void testMinimizedCapturePreservesPreMinimizePlacement()
    {
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
        wta->setEngines(snap.get(), nullptr);

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

        wta->setEngines(snap.get(), nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
        wta->setWindowRegistry(nullptr);
    }

    void testMinimizedCloseRebindsChangedAppPrefixWithoutLosingPlacement()
    {
        // A minimized window closing under a MUTATED appId prefix must re-key its
        // durable record to the current windowId instead of stranding it under
        // the old prefix (where a reopen under the new class could never find it).
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(QStringLiteral("DP-1"), QRect(0, 0, 3072, 1728), QStringLiteral("DP-1"));
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
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
        wta->setEngines(snap.get(), &tileEngine);

        const QString windowId = QStringLiteral("app|closed-on-tile");
        const QString screenId = QStringLiteral("DP-1");
        const QString appId = wta->service()->currentAppIdFor(windowId);
        const QRect tileRect(1540, 8, 1524, 829);

        // Untracked by snap (no float/zone state) and the stub tile engine's
        // capturePlacement default returns nullopt — the close capture takes
        // the engine-miss fallback. The frame still equals the remembered
        // tile rect, so the fallback must record NOTHING.
        wta->setFrameGeometry(windowId, tileRect.x(), tileRect.y(), tileRect.width(), tileRect.height());
        tileEngine.managedRect = tileRect;
        wta->captureWindowPlacement(windowId, screenId);
        QVERIFY2(!wta->service()->placementStore().peek(windowId, appId),
                 "a close frame still on the tile rect must not become the reopen float-back");

        // A close frame off the tile rect records the genuine free position.
        const QRect freeFrame(600, 400, 900, 700);
        wta->setFrameGeometry(windowId, freeFrame.x(), freeFrame.y(), freeFrame.width(), freeFrame.height());
        wta->captureWindowPlacement(windowId, screenId);
        const auto rec = wta->service()->placementStore().peek(windowId, appId);
        QVERIFY(rec);
        QCOMPARE(rec->freeGeometryFor(screenId), freeFrame);

        wta->setEngines(snap.get(), nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
    }
};

QTEST_MAIN(TestWtaCaptureGuards)
#include "test_wta_capture_guards.moc"

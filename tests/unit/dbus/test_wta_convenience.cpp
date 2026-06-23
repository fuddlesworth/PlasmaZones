// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_convenience.cpp
 * @brief Unit tests for WindowTrackingAdaptor convenience methods:
 *        moveWindowToZone, swapWindowsById, getWindowState, getAllWindowStates,
 *        and windowStateChanged signal emission.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <QRectF>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include "FakeScreenProvider.h"
#include "core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "dbus/snapadaptor.h"
#include "dbus/windowtrackingadaptor.h"
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsConvenience = StubSettings;

// =========================================================================
// Stub PhosphorZones::Zone Detector
// =========================================================================

class StubZoneDetectorConvenience : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorConvenience(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(PhosphorZones::Layout* layout) override
    {
        m_layout = layout;
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    PhosphorZones::Layout* m_layout = nullptr;
};

// =========================================================================
// Helpers
// =========================================================================

static PhosphorZones::Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new PhosphorZones::Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new PhosphorZones::Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

// =========================================================================
// Test Class
// =========================================================================

class TestWtaConvenience : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettingsConvenience(nullptr);
        m_zoneDetector = new StubZoneDetectorConvenience(nullptr);

        // WTA needs a parent QObject for QDBusAbstractAdaptor
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);

        m_snapEngine = new SnapEngine(m_layoutManager, m_wta->service(), m_zoneDetector, nullptr, nullptr);
        m_snapEngine->setEngineSettings(m_settings);
        m_wta->service()->setSnapState(m_snapEngine->snapState());
        m_wta->service()->setSnapEngine(m_snapEngine);
        m_wta->setEngines(m_snapEngine, nullptr);

        m_snapAdaptor = new SnapAdaptor(m_snapEngine, m_wta, m_settings, m_parent);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (PhosphorZones::Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }

        m_screenId = QStringLiteral("DP-1");
    }

    void cleanup()
    {
        // SnapAdaptor is owned by m_parent (QDBusAbstractAdaptor parent)
        // Clear engine before deleting to disconnect signals
        if (m_snapAdaptor) {
            m_snapAdaptor->clearEngine();
        }
        m_snapAdaptor = nullptr;
        // WTA is owned by m_parent (QDBusAbstractAdaptor parent)
        m_wta->service()->setSnapState(nullptr);
        delete m_snapEngine;
        m_snapEngine = nullptr;
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // =====================================================================
    // moveWindowToZone
    // =====================================================================

    void testMoveWindowToZone_validZone_emitsApplyGeometry()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Assign a screen mapping so resolveScreenForSnap works
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(windowId, m_zoneIds[0]);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId);
        // Geometry args (x, y, width, height) should have valid dimensions
        QVERIFY(spy.at(0).at(3).toInt() > 0); // width
        QVERIFY(spy.at(0).at(4).toInt() > 0); // height
        QCOMPARE(spy.at(0).at(5).toString(), m_zoneIds[0]);
    }

    void testMoveWindowToZone_invalidZone_noSignal()
    {
        QString windowId = QStringLiteral("firefox|12345");
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(windowId, QStringLiteral("nonexistent-zone-id"));

        QCOMPARE(spy.count(), 0);
    }

    void testMoveWindowToZone_emptyWindowId_noSignal()
    {
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->moveWindowToZone(QString(), m_zoneIds[0]);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // unfloat-to-zone re-snap discriminator
    // =====================================================================

    void testUnfloatToZone_emitsApplyGeometryWithZoneId()
    {
        // Regression: unfloat-to-zone IS a snap commit, so its
        // applyGeometryRequested must carry the (representative) zone id, NOT an
        // empty string. The KWin effect uses an empty zoneId as the
        // "float-restore" discriminator (→ clearWindowSnapped, which strips the
        // snap title-bar / border chrome) and a non-empty one as the "snap
        // commit" discriminator (→ markWindowSnapped, which re-applies it). An
        // empty zoneId here left a re-snapped window wearing its floating chrome.
        const QString windowId = QStringLiteral("firefox|float-resnap");
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Snap, then float — floating captures the pre-float zone.
        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);
        m_snapEngine->toggleWindowFloat(windowId, m_screenId);
        QVERIFY(m_snapEngine->snapState()->isFloating(windowId));

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        // Unfloat back to the zone — a snap commit, not a float-restore.
        m_snapEngine->toggleWindowFloat(windowId, m_screenId);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), windowId);
        // zoneId (arg index 5) must be the original zone, not empty.
        QCOMPARE(spy.at(0).at(5).toString(), m_zoneIds[0]);
    }

    // =====================================================================
    // reapplyWindowAppearance — daemon-driven, engine-common chrome re-apply
    // =====================================================================

    void testReapplyWindowAppearance_reemitsSnappedSkipsFloating()
    {
        // On a compositor-bridge reconnect the daemon must re-drive the chrome
        // (snap border / hidden title bar) for already-snapped windows — the
        // effect cleared it on daemon loss. reapplyWindowAppearance fans out
        // through the common IPlacementEngine API; the snap engine re-emits a
        // snap-commit applyGeometryRequested (non-empty zoneId) per snapped,
        // non-floating window, without moving anything.
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        const QString snapped1 = QStringLiteral("app1|reapply-1");
        const QString snapped2 = QStringLiteral("app2|reapply-2");
        const QString floated = QStringLiteral("app3|reapply-float");

        m_snapEngine->commitSnap(snapped1, m_zoneIds[0], m_screenId);
        m_snapEngine->commitSnap(snapped2, m_zoneIds[1], m_screenId);
        m_snapEngine->commitSnap(floated, m_zoneIds[2], m_screenId);
        m_snapEngine->setWindowFloat(floated, true); // leaves the snapped set

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_wta->reapplyWindowAppearance();

        // One snap-commit emission per snapped, non-floating window; floated skipped.
        QCOMPARE(spy.count(), 2);
        QSet<QString> emittedWindows;
        QSet<QString> emittedZones;
        for (const auto& args : spy) {
            QVERIFY(!args.at(5).toString().isEmpty()); // non-empty zoneId = snap commit
            emittedWindows.insert(args.at(0).toString());
            emittedZones.insert(args.at(5).toString());
        }
        QVERIFY(emittedWindows.contains(snapped1));
        QVERIFY(emittedWindows.contains(snapped2));
        QVERIFY(!emittedWindows.contains(floated));
        QVERIFY(emittedZones.contains(m_zoneIds[0]));
        QVERIFY(emittedZones.contains(m_zoneIds[1]));
    }

    // =====================================================================
    // float-restore — close-while-floating → reopen-floating (unified store)
    // =====================================================================

    void testFloatRestore_closeWhileFloating_reopensFloatingAtGeometry()
    {
        // End-to-end: a snapped window is floated (and moved), then closed while
        // floating. WindowTrackingAdaptor::windowClosed captures a floated
        // WindowPlacement into the unified store, keyed by appId. A reopened
        // instance of the SAME app consumes it via resolveWindowRestore: NOT
        // snapped (shouldSnap=false), placed at the floated geometry via
        // applyGeometryRequested with an EMPTY zoneId, and marked floating.
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        const QString w1 = QStringLiteral("settings|float-old");
        const QRect floatedGeo(123, 456, 800, 600);

        m_snapEngine->commitSnap(w1, m_zoneIds[0], m_screenId);
        m_wta->setFrameGeometry(w1, floatedGeo.x(), floatedGeo.y(), floatedGeo.width(), floatedGeo.height());
        m_snapEngine->setWindowFloat(w1, true);
        QVERIFY(m_snapEngine->snapState()->isFloating(w1));

        // Close while floating → captures a floated placement for app "settings".
        m_wta->windowClosed(w1, static_cast<int>(PhosphorEngine::WindowKind::Normal));
        QVERIFY(m_wta->service()->placementStore().contains(w1, QStringLiteral("settings")));

        // Reopen as a new instance of the same app.
        const QString w2 = QStringLiteral("settings|float-new");
        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);
        int x = 0, y = 0, wd = 0, h = 0;
        bool shouldSnap = true;
        m_snapAdaptor->resolveWindowRestore(w2, m_screenId, false, static_cast<int>(PhosphorEngine::WindowKind::Normal),
                                            x, y, wd, h, shouldSnap);

        QCOMPARE(shouldSnap, false); // float-restore is not a snap
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), w2);
        // Empty zoneId (index 5) = float/unmanaged discriminator (no snap border).
        QCOMPARE(spy.at(0).at(5).toString(), QString());
        QCOMPARE(
            QRect(spy.at(0).at(1).toInt(), spy.at(0).at(2).toInt(), spy.at(0).at(3).toInt(), spy.at(0).at(4).toInt()),
            floatedGeo);
        QVERIFY(m_snapEngine->snapState()->isFloating(w2)); // reopened floating, not snapped
        // The record is re-recorded under the LIVE windowId (w2) so the window's
        // float-back survives logout/login — KWin assigns a new uuid at login, so the
        // record matched by appId FIFO, and consuming it (the old behaviour) lost the
        // float-back, leaving a later float with no recorded free position. The stale
        // recorded-uuid entry (w1) is rebound, not left as a duplicate.
        QVERIFY(m_wta->service()->placementStore().contains(w2));
        QVERIFY(!m_wta->service()->placementStore().contains(w1));
        // Pre-float zone is restored so a subsequent float-toggle resnaps the
        // window back into its original zone (unfloatToZone reads preFloatZones).
        QCOMPARE(m_snapEngine->snapState()->preFloatZones(w2), QStringList{m_zoneIds[0]});
    }

    void testFloatRestore_loadedAssignmentDoesNotMaskFloatedRecord()
    {
        // Daemon-only restart regression: the old WindowZoneAssignmentsFull is
        // still loaded, so a window that was FLOATED comes back with its zone
        // assignment intact (floating keeps the assignment) → isWindowSnapped()
        // == true. The unified store MUST be consulted before the legacy
        // "already has assignment, skipping" path, otherwise the window stays
        // snapped instead of floating. Same uuid (daemon restart), so the record
        // is found uuid-exact.
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        const QString w = QStringLiteral("settings|restart-same-uuid");
        const QRect floatedGeo(321, 654, 700, 500);

        // Simulate post-restart load: window has its zone assignment (snapped)…
        m_snapEngine->commitSnap(w, m_zoneIds[0], m_screenId);
        QVERIFY(m_snapEngine->snapState()->isWindowSnapped(w));
        // …and a floated placement record persisted for it.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("settings");
        rec.screenId = m_screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        slot.zoneIds = QStringList{m_zoneIds[0]}; // pre-float zones
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(m_screenId, floatedGeo);
        m_wta->service()->placementStore().record(rec);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);
        int x = 0, y = 0, wd = 0, h = 0;
        bool shouldSnap = true;
        m_snapAdaptor->resolveWindowRestore(w, m_screenId, false, static_cast<int>(PhosphorEngine::WindowKind::Normal),
                                            x, y, wd, h, shouldSnap);

        QCOMPARE(shouldSnap, false); // floated, NOT snapped despite the loaded assignment
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(5).toString(), QString()); // empty zoneId = float (no border)
        QCOMPARE(
            QRect(spy.at(0).at(1).toInt(), spy.at(0).at(2).toInt(), spy.at(0).at(3).toInt(), spy.at(0).at(4).toInt()),
            floatedGeo);
        QVERIFY(m_snapEngine->snapState()->isFloating(w));
        // Same-uuid restart re-records the floated entry so it survives further restarts.
        QVERIFY(m_wta->service()->placementStore().contains(w, QStringLiteral("settings")));
    }

    void testFloatToggle_usesPlacementRecordFloatBack()
    {
        // Float-back source of truth: toggling a snapped window to floating must
        // restore it to the float-back geometry carried by its unified placement
        // record — NOT the legacy m_unmanagedGeometries store (which is uuid-keyed
        // and dropped on load by the disabled-context gate). The record survives
        // where the legacy store does not.
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        const QString w = QStringLiteral("settings|floatback");
        const QRect floatBack(271, 314, 962, 655);

        m_snapEngine->commitSnap(w, m_zoneIds[0], m_screenId);
        // Record a snapped placement carrying the float-back, and ensure the legacy
        // store has NOTHING (simulates the post-restart disabled-context-drop case).
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("settings");
        rec.screenId = m_screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{m_zoneIds[0]};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(m_screenId, floatBack); // shared float-back (single store)
        m_wta->service()->placementStore().record(rec);

        QSignalSpy spy(m_snapEngine, &PhosphorSnapEngine::SnapEngine::applyGeometryRequested);
        m_snapEngine->setWindowFloat(w, true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(5).toString(), QString()); // empty zoneId = float
        QCOMPARE(
            QRect(spy.at(0).at(1).toInt(), spy.at(0).at(2).toInt(), spy.at(0).at(3).toInt(), spy.at(0).at(4).toInt()),
            floatBack);
    }

    void testFloatRestore_openFloatingWindowSnapshot()
    {
        // A floating window that is still OPEN (never closed) must be captured into
        // the unified store at save time, so its floating geometry survives a
        // daemon restart (windows stay open → no windowClosed → no close-recorded
        // entry). saveState() calls refreshOpenWindowPlacements() to do this.
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        const QString w1 = QStringLiteral("settings|open-float");
        const QRect floatedGeo(200, 300, 900, 700);

        m_snapEngine->commitSnap(w1, m_zoneIds[0], m_screenId);
        m_wta->setFrameGeometry(w1, floatedGeo.x(), floatedGeo.y(), floatedGeo.width(), floatedGeo.height());
        m_snapEngine->setWindowFloat(w1, true);
        QVERIFY(m_wta->service()->isWindowFloating(w1));

        m_wta->refreshOpenWindowPlacements();

        // The store now holds a floated record for the open window at its live geo.
        auto rec = m_wta->service()->placementStore().peek(w1, QStringLiteral("settings"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QCOMPARE(rec->freeGeometryFor(m_screenId), floatedGeo);
    }

    void testFloatRestore_openFloatingWindow_emptyEngineScreen_resolvesFromFrame()
    {
        // Regression (settings-app float-geometry not restored across logout/login):
        // a window FLOATED without a tracked screen assignment reports an EMPTY
        // screenId from capturePlacement. The save-time sync
        // (refreshOpenWindowPlacements → captureWindowPlacement) previously gated the
        // freeGeometry write on `!screenId.isEmpty()`, so such a window's live float
        // geometry was SILENTLY DROPPED: never persisted, so a later re-float or
        // logout→login had no freeGeometry to restore. The fix resolves the screen
        // from the live frame's position, so a floating window's geometry is always
        // captured.
        //
        // A local WTA wired to a deterministic ScreenManager (FakeScreenProvider) is
        // used so the frame → screen resolution is independent of the test QPA
        // (offscreen has no usable screen identifier). The shared fixture's WTA has a
        // null ScreenManager, which is the very gap this exercises.
        const QString screenId = QStringLiteral("DP-9");
        const QRect screenRect(0, 0, 1920, 1080);
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(screenId, screenRect, screenId); // before the manager so it is in the initial snapshot
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start(); // ingest the provider's screens into the tracked snapshot
        QCOMPARE(screenMgr.effectiveScreenAt(QPoint(500, 400)), screenId); // resolution sanity

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* snap = new SnapEngine(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap->setEngineSettings(m_settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap);
        wta->setEngines(snap, nullptr);

        const QRect floatedGeo(120, 90, 760, 540); // inside screenRect
        const QString w1 = QStringLiteral("settings|orphan-float");
        // Float WITHOUT a prior snap → the snap engine never recorded a screen
        // assignment, so capturePlacement comes back with an empty screenId.
        snap->setWindowFloat(w1, true);
        QVERIFY(wta->service()->isWindowFloating(w1));
        auto captured = snap->capturePlacement(w1);
        QVERIFY(captured.has_value());
        QVERIFY(captured->screenId.isEmpty()); // precondition the old gate tripped on

        wta->setFrameGeometry(w1, floatedGeo.x(), floatedGeo.y(), floatedGeo.width(), floatedGeo.height());
        wta->refreshOpenWindowPlacements();

        auto rec = wta->service()->placementStore().peek(w1, QStringLiteral("settings"));
        QVERIFY(rec.has_value());
        // The live float geometry is persisted despite the engine's empty screenId,
        // keyed by the screen resolved from the frame's position.
        QCOMPARE(rec->freeGeometryFor(screenId), floatedGeo);

        // Tear down the engine before the parent destructor deletes wta so the
        // service never dereferences a dangling snap engine/state.
        wta->service()->setSnapEngine(nullptr);
        wta->service()->setSnapState(nullptr);
        delete snap;
    }

    void testFloatRestore_tiledFrameNeverPoisonsSnapFloatBack()
    {
        // Regression (Steam "tiled geometry restored to floated in snapping mode"):
        // the shared free/float geometry is written by captureWindowPlacement from
        // the live frame whenever the OWNING engine reports `floating`. Across a mode
        // flip, a window TILED by autotile becomes snap-owned and snap reports
        // `floating`, yet the live frame still holds the autotile TILE rect (the
        // window has not been repositioned). Writing it poisons the float-back, and a
        // later snap reopen restores the tile rect as the floated position. The write
        // must be refused while the autotile engine still reports the window tiled —
        // the genuine prior free geometry stays intact.
        const QString screenId = QStringLiteral("DP-9");
        const QRect screenRect(0, 0, 3840, 2160);
        PhosphorScreens::FakeScreenProvider fake;
        fake.addScreen(screenId, screenRect, screenId);
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.screenProvider = &fake, .useGeometrySensors = false});
        screenMgr.start();

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* snap = new SnapEngine(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap->setEngineSettings(m_settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap);
        wta->setEngines(snap, nullptr);

        const QString w1 = QStringLiteral("steam|abc123");
        const QRect goodFloat(918, 624, 1608, 957); // a genuine floated geometry
        const QRect tileRect(8, 1138, 3184, 602); // a full-width bottom-row tile rect

        // The autotile engine still reports this window actively tiled.
        wta->service()->setAutotileTiledPredicate([&](const QString& id) {
            return id == w1;
        });

        // Seed a genuine prior free geometry (as an earlier honest snap-float capture
        // would have). Recorded directly so the seed is independent of the guard.
        PhosphorEngine::WindowPlacement seed;
        seed.windowId = w1;
        seed.appId = QStringLiteral("steam");
        seed.screenId = screenId;
        PhosphorEngine::EngineSlot snapSlot;
        snapSlot.state = PhosphorEngine::WindowPlacement::stateFloating();
        seed.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), snapSlot);
        seed.freeGeometryByScreen.insert(screenId, goodFloat);
        wta->service()->placementStore().record(seed);

        // Now the window is snap-floating but its live frame is the TILE rect.
        snap->setWindowFloat(w1, true);
        QVERIFY(wta->service()->isWindowFloating(w1));
        wta->setFrameGeometry(w1, tileRect.x(), tileRect.y(), tileRect.width(), tileRect.height());

        wta->captureWindowPlacement(w1);

        // The tile rect must NOT have overwritten the genuine float-back.
        auto rec = wta->service()->placementStore().peek(w1, QStringLiteral("steam"));
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryFor(screenId), goodFloat);
        QVERIFY(rec->freeGeometryFor(screenId) != tileRect);

        wta->service()->setAutotileTiledPredicate({});
        wta->service()->setSnapEngine(nullptr);
        wta->service()->setSnapState(nullptr);
        delete snap;
    }

    // =====================================================================
    // swapWindowsById
    // =====================================================================

    void testSwapWindowsById_twoSnappedWindows_emitsTwoApplyGeometry()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Snap both windows to different zones via the WTA's windowSnapped slot
        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);
        m_snapEngine->commitSnap(window2, m_zoneIds[1], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 2);

        // Window1 should move to zone2, window2 to zone1
        QCOMPARE(spy.at(0).at(0).toString(), window1);
        QCOMPARE(spy.at(0).at(5).toString(), m_zoneIds[1]);
        QCOMPARE(spy.at(1).at(0).toString(), window2);
        QCOMPARE(spy.at(1).at(5).toString(), m_zoneIds[0]);
    }

    void testSwapWindowsById_oneNotSnapped_noSignal()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);

        // Only snap window1
        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::applyGeometryRequested);

        m_snapAdaptor->swapWindowsById(window1, window2);

        QCOMPARE(spy.count(), 0);
    }

    // =====================================================================
    // getWindowState
    // =====================================================================

    void testGetWindowState_snappedWindow_returnsStruct()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        PhosphorProtocol::WindowStateEntry state = m_wta->getWindowState(windowId);
        QCOMPARE(state.windowId, windowId);
        QCOMPARE(state.zoneId, m_zoneIds[0]);
        QCOMPARE(state.screenId, m_screenId);
        QCOMPARE(state.isFloating, false);
    }

    void testGetWindowState_floatingWindow_returnsFloatingTrue()
    {
        QString windowId = QStringLiteral("firefox|12345");

        // Snap then float
        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);
        m_wta->setWindowFloating(windowId, true);

        PhosphorProtocol::WindowStateEntry state = m_wta->getWindowState(windowId);
        QCOMPARE(state.isFloating, true);
    }

    void testGetWindowState_unknownWindow_returnsEmptyZone()
    {
        QString windowId = QStringLiteral("unknown|99999");

        PhosphorProtocol::WindowStateEntry state = m_wta->getWindowState(windowId);
        QVERIFY(state.zoneId.isEmpty());
    }

    // =====================================================================
    // getAllWindowStates
    // =====================================================================

    void testGetAllWindowStates_multipleWindows_returnsList()
    {
        QString window1 = QStringLiteral("app1|11111");
        QString window2 = QStringLiteral("app2|22222");

        m_snapEngine->commitSnap(window1, m_zoneIds[0], m_screenId);
        m_snapEngine->commitSnap(window2, m_zoneIds[1], m_screenId);

        PhosphorProtocol::WindowStateList allStates = m_wta->getAllWindowStates();
        QCOMPARE(allStates.size(), 2);

        // Collect all window IDs from the list
        QStringList windowIds;
        for (const auto& ws : allStates) {
            windowIds.append(ws.windowId);
        }
        QVERIFY(windowIds.contains(window1));
        QVERIFY(windowIds.contains(window2));
    }

    // =====================================================================
    // windowStateChanged signal
    // =====================================================================

    void testWindowStateChanged_emittedOnSnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QVERIFY(spy.count() >= 1);

        // Find the "snapped" emission
        bool foundSnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (state.changeType == QLatin1String("snapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundSnapped = true;
                break;
            }
        }
        QVERIFY(foundSnapped);
    }

    void testWindowStateChanged_emittedOnUnsnap()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_snapEngine->uncommitSnap(windowId);

        QVERIFY(spy.count() >= 1);

        bool foundUnsnapped = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (state.changeType == QLatin1String("unsnapped")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundUnsnapped = true;
                break;
            }
        }
        QVERIFY(foundUnsnapped);
    }

    void testWindowStateChanged_emittedOnFloat()
    {
        QString windowId = QStringLiteral("firefox|12345");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], m_screenId);

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);

        m_wta->setWindowFloating(windowId, true);

        QVERIFY(spy.count() >= 1);

        bool foundFloated = false;
        for (int i = 0; i < spy.count(); ++i) {
            auto state = spy.at(i).at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (state.changeType == QLatin1String("floated")) {
                QCOMPARE(spy.at(i).at(0).toString(), windowId);
                foundFloated = true;
                break;
            }
        }
        QVERIFY(foundFloated);
    }

    // Regression: under the per-engine float model the owning engine flips its
    // float bit BEFORE the daemon's sync slot reaches WTA::setWindowFloating
    // (e.g. AutotileEngine::performToggleFloat toggles then emits
    // windowFloatingChanged → Daemon::syncAutotileFloatState → here). A re-query
    // of the service therefore already reports floating==true, so the old
    // broadcast gate (which compared against m_service->isWindowFloating())
    // suppressed EVERY autotile float broadcast — leaving the effect's float
    // cache stale and silently breaking the autotile FFM float-pause. The gate
    // must instead compare against the last value WTA broadcast.
    void testSetWindowFloating_broadcastsWhenServiceAlreadyReportsFloating()
    {
        const QString windowId = QStringLiteral("firefox|12345");

        // Simulate the engine having ALREADY set the float bit: the resolver
        // (consulted by m_service->isWindowFloating) reports floating regardless
        // of WTS's own set.
        m_wta->service()->setEngineFloatResolver([windowId](const QString& id) {
            return id == windowId;
        });

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowFloatingChanged);

        // The real float edge must broadcast even though a re-query says true.
        m_wta->setWindowFloating(windowId, true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), windowId);
        QCOMPARE(spy.first().at(1).toBool(), true);

        // Dedup still holds: setting the same broadcast value again is a no-op.
        m_wta->setWindowFloating(windowId, true);
        QCOMPARE(spy.count(), 1);

        // The unfloat edge broadcasts.
        m_wta->setWindowFloating(windowId, false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toBool(), false);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettingsConvenience* m_settings = nullptr;
    StubZoneDetectorConvenience* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
    SnapAdaptor* m_snapAdaptor = nullptr;
    SnapEngine* m_snapEngine = nullptr;
    PhosphorZones::Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
    QString m_screenId;
};

QTEST_MAIN(TestWtaConvenience)
#include "test_wta_convenience.moc"

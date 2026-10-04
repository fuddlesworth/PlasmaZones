// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_float_back_model.cpp
 * @brief The float-back refusal model at its three writers
 *        (WindowTrackingService::recordFreeGeometry, recordFloatingClose and
 *        WindowTrackingAdaptor::captureWindowPlacement): a rect off its screen
 *        key (P), the frame of a window in a zone or tile in view (O), a
 *        managed frame (M), and, where a frame is sampled, the frame of a
 *        minimized, suspended or output-filling window (S). Two outputs side
 *        by side, a snap engine on per-screen stores, a stub tiling engine in
 *        the autotile slot and a window registry.
 */

#include <QTest>
#include <QRect>
#include <QString>
#include <memory>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "FakePhysicalScreenSource.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubPlacementEngine.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PhosphorEngine::WindowPlacement;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kLeft = QStringLiteral("DP-1");
const QString kRight = QStringLiteral("DP-2");
const QRect kLeftRect(0, 0, 1920, 1080);
const QRect kRightRect(1920, 0, 1920, 1080);

struct FloatBackFixture
{
    FloatBackFixture()
    {
        fake.addScreen(kLeft, kLeftRect, kLeft);
        fake.addScreen(kRight, kRightRect, kRight);
        screenMgr = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr->start();
        layouts = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        layout = createTestLayout(3, layouts);
        layouts->addLayout(layout);
        layouts->setActiveLayout(layout);
        wta = new WindowTrackingAdaptor(layouts, &detector, screenMgr.get(), &settings, nullptr, nullptr, &parent);
        wta->setWindowRegistry(&registry);
        snap = std::make_unique<PhosphorSnapEngine::SnapEngine>(layouts, wta->service(), &detector, nullptr, nullptr);
        snap->setEngineSettings(&settings);
        snap->setWindowRegistry(&registry);
        snap->setNavigationStateProvider(wta);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        PhosphorPlacement::WindowTrackingService::SnapStateResolver resolver;
        PhosphorSnapEngine::SnapEngine* e = snap.get();
        resolver.forWindow = [e](const QString& id) {
            return e->stateForWindow(id);
        };
        resolver.forWindowOnScreen = [e](const QString& id, const QString& s, int desktop) {
            return e->stateForWindowOnScreen(id, s, desktop);
        };
        resolver.forScreen = [e](const QString& s) {
            return static_cast<PhosphorSnapEngine::SnapState*>(e->stateForScreen(s));
        };
        resolver.globals = [e]() {
            return e->globalState();
        };
        resolver.allStates = [e]() {
            return e->allSnapStates();
        };
        resolver.forgetWindow = [e](const QString& id) {
            e->forgetWindow(id);
        };
        resolver.holdsWindow = [e](const QString& id, const PhosphorSnapEngine::SnapState* state) {
            return e->holdsWindowInState(id, state);
        };
        wta->service()->setSnapStateResolver(resolver);
        wta->setEngines(snap.get(), &tiling, nullptr);
        snap->setCurrentDesktopForScreen(kLeft, 1);
        snap->setCurrentDesktopForScreen(kRight, 1);
    }
    ~FloatBackFixture()
    {
        wta->setEngines(nullptr, nullptr, nullptr);
        snap->setNavigationStateProvider(nullptr);
        snap->setWindowRegistry(nullptr);
        wta->service()->setSnapState(nullptr);
        wta->service()->setSnapEngine(nullptr);
        snap.reset();
        delete layouts;
    }

    /// Register @p instance (app "app") with the given window state. Visible by
    /// default: a registered window with no minimize state takes the capture's
    /// minimized-preserve branch, which would make every capture row vacuous.
    QString registerWindow(const QString& instance, std::optional<bool> maximized = {},
                           std::optional<bool> fullscreen = {}, std::optional<bool> minimized = false)
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMaximized = maximized;
        meta.isFullscreen = fullscreen;
        meta.isMinimized = minimized;
        registry.upsert(instance, meta);
        const QString windowId = QStringLiteral("app|") + instance;
        registry.canonicalizeWindowId(windowId);
        return windowId;
    }
    QString zone(int index) const
    {
        return layout->zones().at(index)->id().toString();
    }
    QRect zoneRect(int index, const QString& screen) const
    {
        return wta->service()->resolveZoneGeometry({zone(index)}, screen);
    }
    void setFrame(const QString& windowId, const QRect& r)
    {
        wta->setFrameGeometry(windowId, r.x(), r.y(), r.width(), r.height());
    }
    QRect floatBack(const QString& windowId, const QString& screen) const
    {
        const auto rec = wta->service()->placementStore().peekExact(windowId);
        return rec ? rec->freeGeometryByScreen.value(screen) : QRect();
    }
    /// @p windowId snapped into zone 0 on @p screen, then snap-floated there.
    void snapThenFloat(const QString& windowId, const QString& screen)
    {
        snap->commitSnap(windowId, zone(0), screen);
        snap->setWindowFloat(windowId, true, screen);
    }

    IsolatedConfigGuard guard;
    PhosphorScreens::FakePhysicalScreenSource fake;
    std::unique_ptr<PhosphorScreens::ScreenManager> screenMgr;
    StubPlacementEngine tiling; // outlives the adaptor
    PhosphorEngine::WindowRegistry registry;
    QObject parent;
    StubZoneDetector detector;
    StubSettings settings;
    PhosphorZones::LayoutRegistry* layouts = nullptr;
    PhosphorZones::Layout* layout = nullptr;
    WindowTrackingAdaptor* wta = nullptr; // parent-owned
    std::unique_ptr<PhosphorSnapEngine::SnapEngine> snap;
};
} // namespace

class TestWtaFloatBackModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // (S) at the capture: a maximized window's frame fills the output and is
    // not a free position, so the float-back recorded before stays (F157).
    void captureRefusesAMaximizedFloatingFrame()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f157-capture"));
        f.snapThenFloat(w, kLeft);
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        QCOMPARE(f.floatBack(w, kLeft), free);
        f.registerWindow(QStringLiteral("f157-capture"), true);
        f.setFrame(w, kLeftRect);

        f.wta->captureWindowPlacement(w);

        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // (S) at the close fallback: a fullscreen window's frame is refused, and
    // the close still adopts the screen it closed on (F157).
    void closeFallbackRefusesAFullscreenFrameButAdoptsTheScreen()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f157-close"), false, true);
        f.setFrame(w, kRightRect);

        f.wta->captureWindowPlacement(w, kRight);

        const auto rec = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->screenId, kRight);
        QVERIFY(!rec->freeGeometryByScreen.value(kRight).isValid());
    }

    // (P) at the capture: a window re-homed to DP-2 whose frame shadow is
    // still on DP-1 files nothing under DP-2; a frame on DP-2 is filed (F365).
    void captureRefusesAFrameFiledUnderTheWrongScreen()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f365"));
        f.snapThenFloat(w, kRight);
        f.setFrame(w, QRect(100, 100, 400, 300));

        f.wta->captureWindowPlacement(w);
        QVERIFY(!f.floatBack(w, kRight).isValid());

        const QRect onRight(2100, 100, 400, 300);
        f.setFrame(w, onRight);
        f.wta->captureWindowPlacement(w);
        QCOMPARE(f.floatBack(w, kRight), onRight);
    }

    // (O) is the LIVE hold: a zone remembered on a screen that now tiles does
    // not refuse a genuine free frame of the window (F487).
    void recordFreeGeometryKeepsAGenuineFrameOfAWindowSnappedOnAFlippedContext()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f487"));
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.snap->setLiveModeResolver([](const QString& screen) {
            return screen == kLeft ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                   : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        const QRect free(1300, 600, 500, 400);

        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);

        QCOMPARE(f.floatBack(w, kLeft), free);
        f.snap->setLiveModeResolver({});
    }

    // No window-state refusal at the write point: an explicit rect is
    // recorded for a minimized window (F413).
    void recordFreeGeometryAcceptsAnExplicitRectForAMinimizedWindow()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f413"), {}, {}, true);
        const QRect free(300, 200, 640, 480);

        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);

        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // (M): a window floated off its zone that has not moved stands on the
    // zone it floated from, so that rect is refused; a free rect is not
    // (F115, F178).
    void recordFreeGeometryRefusesAPreFloatZoneRect()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f115"));
        f.snapThenFloat(w, kLeft);
        f.wta->service()->clearFreeGeometry(w);

        f.wta->service()->recordFreeGeometry(w, kLeft, f.zoneRect(0, kLeft), true);
        QVERIFY(!f.floatBack(w, kLeft).isValid());

        const QRect free(1300, 600, 500, 400);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // (M): the rect a tiling engine emitted for the window is refused even
    // when the window is not tiled in view (its tile is on another desktop),
    // where the engine-tiled check no longer reads it (F382).
    void recordFreeGeometryRefusesAnEmittedTileRect()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f382"));
        const QRect tile(960, 0, 960, 1080);
        f.tiling.managedRect = tile;

        f.wta->service()->recordFreeGeometry(w, kLeft, tile, true);
        QVERIFY(!f.floatBack(w, kLeft).isValid());

        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // (M) is exact: a rect with a zone's size somewhere else is a free
    // position (F451).
    void recordFreeGeometryAcceptsAZoneSizedRectOffTheZone()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f451"));
        f.snapThenFloat(w, kLeft);
        QRect moved = f.zoneRect(0, kLeft);
        moved.moveTopLeft(moved.topLeft() + QPoint(40, 30));

        f.wta->service()->recordFreeGeometry(w, kLeft, moved, true);

        QCOMPARE(f.floatBack(w, kLeft), moved);
    }

    // (S) at the snap pre-snap capture: a window suspended by a minimize still
    // stands on its zone, and its frame is not recorded over the float-back
    // (F156).
    void snapHelperRefusesASuspensionFloat()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f156"));
        f.snap->commitSnap(w, f.zone(0), kLeft);
        f.wta->service()->markSuspensionFloat(w);
        f.snap->setWindowFloat(w, true, kLeft);
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        QRect zone = f.zoneRect(0, kLeft);
        zone.adjust(0, 0, 0, -1); // not the zone rect itself, so (M) cannot answer
        f.setFrame(w, zone);

        f.snap->recordFreeFrameBeforeUserSnap(w, kLeft);

        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // ── The frame a managed window settled at (M[w]) ─────────────────────

    // A tile whose client settled smaller than its tile (centred inside it)
    // and is then floated still stands on that settled frame: the capture
    // keeps the float-back recorded before (F421).
    void floatOfATileThatSettledOffItsRectKeepsTheFloatBack()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f421"));
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        tileOnLeft(f, w);
        f.setFrame(w, QRect(1060, 140, 760, 800)); // centred inside the tile

        floatTheTile(f, w);
        f.wta->captureWindowPlacement(w);

        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // The drag subject's frames are where the user moves it, not a settled
    // managed frame: released where it was picked up and floated, the drop
    // point is its float-back (F452).
    void aDragReleaseOfATileIsAFloatBack()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f452"));
        tileOnLeft(f, w);
        const QRect dropped(500, 300, 760, 800);
        f.wta->setInteractiveDragWindow(w);
        f.setFrame(w, dropped);
        f.wta->setInteractiveDragWindow(QString());

        floatTheTile(f, w);
        f.wta->captureWindowPlacement(w);

        QCOMPARE(f.floatBack(w, kLeft), dropped);
    }

    // A tile's settled frame read once the window is no longer tiled in view
    // (its tile is on another desktop) is still not a float-back (F382).
    void aSettledTileFrameReadOffViewIsNotAFloatBack()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f382-settled"));
        tileOnLeft(f, w);
        const QRect settled(1060, 140, 760, 800);
        f.setFrame(w, settled);
        f.tiling.heldScreen.remove(w);

        f.wta->service()->recordFreeGeometry(w, kLeft, settled, true);

        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

    // A size-increment client snapped short of its zone settles off the zone
    // rect; once the zone is dropped (Restore, a keyboard move out), that
    // settled frame is still not a free position (F221).
    void aFrameSettledInAZoneIsNotAFloatBackOnceTheZoneIsDropped()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f221"));
        f.snap->commitSnap(w, f.zone(0), kLeft);
        QRect settled = f.zoneRect(0, kLeft);
        settled.adjust(0, 0, -2, -2);
        f.setFrame(w, settled);
        f.wta->service()->unassignWindow(w);

        f.wta->service()->recordFreeGeometry(w, kLeft, settled, true);

        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

private:
    /// @p w tiled on DP-1 by the stub engine, which emitted the whole right
    /// half of the output for it; the tracking service reads it as tiled in
    /// view, as the daemon's predicate does.
    static void tileOnLeft(FloatBackFixture& f, const QString& w)
    {
        f.tiling.heldScreen.insert(w, kLeft);
        f.tiling.managedRect = QRect(960, 0, 960, 1080);
        f.wta->service()->setEngineTiledPredicate([&f](const QString& id) {
            return f.tiling.heldScreen.contains(id);
        });
    }
    /// The stub engine floats @p w: it is no longer tiled, and its capture
    /// answers a floating slot.
    static void floatTheTile(FloatBackFixture& f, const QString& w)
    {
        f.tiling.heldScreen.remove(w);
        f.tiling.trackedElsewhere.insert(w);
        f.tiling.captureState = WindowPlacement::stateFloating();
    }
};

QTEST_MAIN(TestWtaFloatBackModel)
#include "test_wta_float_back_model.moc"

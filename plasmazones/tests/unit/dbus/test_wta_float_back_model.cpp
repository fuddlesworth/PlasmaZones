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
 *        the autotile slot and a window registry. Plus the float verbs that
 *        feed those writers: the bus setWindowFloating, its screen form and
 *        the in-process sync.
 */

#include "wta_float_back_fixture.h"

#include "helpers/ScreenDisablingResolver.h"
#include "helpers/WindowPlacementBuilders.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QScopeGuard>
#include <QSignalSpy>

#include <optional>

/// The object an adaptor is exported through, as Daemon is: Qt sets a bus
/// call's QDBusContext on it, not on the adaptor (DbusHelpers::busCaller).
class BusHost : public QObject, public QDBusContext
{
    Q_OBJECT
};

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

    // (S) at the snap pre-snap capture: a minimized window's frame is the
    // hidden rect, so it is not recorded over the float-back. The write point
    // no longer refuses it (F413), so this is where the refusal lives (F816).
    void snapHelperRefusesAMinimizedWindow()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f816"));
        f.snapThenFloat(w, kLeft);
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        f.setFrame(w, QRect(900, 500, 400, 300));
        f.registerWindow(QStringLiteral("f816"), {}, {}, true);

        f.snap->recordFreeFrameBeforeUserSnap(w, kLeft);

        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // ── The capture's own branches (F849) ────────────────────────────────

    // A floated window that has not moved stands on the zone it floated from:
    // the capture records nothing (F849 a).
    void captureRefusesTheZoneAFloatedWindowStillStandsOn()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f849a"));
        f.snapThenFloat(w, kLeft);
        f.wta->service()->clearFreeGeometry(w);
        f.setFrame(w, f.zoneRect(0, kLeft));

        f.wta->captureWindowPlacement(w);

        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

    // The zone a window holds on another desktop is a managed frame too: a
    // window floated here that stands on its zone there records nothing
    // (F849 b, the per-desktop form of a).
    void captureRefusesTheZoneTheWindowHoldsOnAnotherDesktop()
    {
        FloatBackFixture f;
        // On desktops 1 and 2, floating on 1 and in zone 2 on desktop 2: the
        // restore on desktop 1 floats it and seeds desktop 2's store.
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMaximized = false;
        meta.isFullscreen = false;
        meta.isMinimized = false;
        meta.virtualDesktop = 1;
        meta.virtualDesktops = {1, 2};
        f.registry.upsert(QStringLiteral("f849b"), meta);
        const QString w = QStringLiteral("app|f849b");
        f.registry.canonicalizeWindowId(w);
        WindowPlacement rec;
        rec.windowId = w;
        rec.appId = QStringLiteral("app");
        rec.screenId = kLeft;
        rec.virtualDesktop = 1;
        PhosphorEngine::EngineSlot slot;
        slot.state = WindowPlacement::stateFloating();
        slot.zonesByDesktop.insert(2, {f.zone(2)});
        rec.engines.insert(WindowPlacement::snapEngineId(), slot);
        QVERIFY(f.wta->service()->placementStore().record(rec));
        (void)f.snap->resolveWindowRestore(w, kLeft, false);
        QVERIFY(f.snap->isFloating(w));
        f.wta->service()->clearFreeGeometry(w);
        const QRect otherDesktopZone = f.zoneRect(2, kLeft);
        QVERIFY(f.wta->service()->isManagedFrame(w, otherDesktopZone));
        f.setFrame(w, otherDesktopZone);

        f.wta->captureWindowPlacement(w);

        QVERIFY(!f.floatBack(w, kLeft).isValid());
    }

    // An unchanged re-capture leaves the store as it was and marks nothing
    // dirty, so an idle window does not re-arm the save timer (F849 c).
    void anUnchangedRecaptureMarksNothingDirty()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f849c"));
        f.snapThenFloat(w, kLeft);
        f.setFrame(w, QRect(300, 200, 640, 480));
        f.wta->captureWindowPlacement(w);
        QVERIFY(f.wta->service()->peekDirty() & PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        f.wta->service()->clearDirty();

        f.wta->captureWindowPlacement(w);

        QVERIFY(!(f.wta->service()->peekDirty() & PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements));
    }

    // A live capture never prunes the app's other pure-float records; the
    // close capture, the freshest word on the app's float-back there, does
    // (F849 d).
    void onlyTheCloseCaptureCollapsesPureFloatSiblings()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f849d"));
        const QString old = QStringLiteral("app|f849d-old");
        QVERIFY(f.wta->service()->placementStore().record(PlasmaZones::TestHelpers::makePlacement(
            old, QStringLiteral("app"), WindowPlacement::stateFloating(), WindowPlacement::snapEngineId(), kLeft,
            QRect(100, 100, 500, 400))));
        f.snapThenFloat(w, kLeft);
        f.setFrame(w, QRect(300, 200, 640, 480));

        f.wta->captureWindowPlacement(w);
        QVERIFY(f.wta->service()->placementStore().contains(old));

        f.wta->captureWindowPlacement(w, kLeft);
        QVERIFY(!f.wta->service()->placementStore().contains(old));
    }

    // A registered window whose minimize state never arrived cannot be shown
    // to be visible: a live capture preserves its record instead of sampling
    // the frame, and a visible one samples it (F849 e).
    void aWindowOfUnknownMinimizeStateKeepsItsRecord()
    {
        FloatBackFixture f;
        const QString w = f.registerWindow(QStringLiteral("f849e"), {}, {}, std::nullopt);
        f.snapThenFloat(w, kLeft);
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        const QRect moved(900, 500, 400, 300);
        f.setFrame(w, moved);

        f.wta->captureWindowPlacement(w);
        QCOMPARE(f.floatBack(w, kLeft), free);

        f.registerWindow(QStringLiteral("f849e"));
        f.wta->captureWindowPlacement(w);
        QCOMPARE(f.floatBack(w, kLeft), moved);
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

    // The bus float verb floats a tile through the engine that runs its
    // monitor and says nothing itself: the engine's own relay is the float
    // edge, so the effect never hears of a float the layout did not make
    // (F678, l18_bus_setwindowfloating).
    void busSetWindowFloatingFloatsATileThroughItsEngine()
    {
        FloatBackFixture f;
        FloatLoggingEngine tile;
        tile.activeScreens = {kRight};
        f.wta->setEngines(f.snap.get(), &tile, nullptr);
        const auto restore = qScopeGuard([&f] {
            f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
        });
        const QString w = f.registerWindow(QStringLiteral("f678-tile"));
        tile.heldScreen.insert(w, kRight);
        f.setFrame(w, QRect(2100, 100, 800, 600));
        QSignalSpy floatSpy(f.wta, &WindowTrackingAdaptor::windowFloatingChanged);

        f.wta->setWindowFloating(w, true);

        QCOMPARE(tile.floatCalls.size(), 1);
        QCOMPARE(tile.floatCalls.first(), (FloatLoggingEngine::Call{w, true, kRight}));
        QVERIFY2(floatSpy.isEmpty(), "only the engine's own relay may report the float");
    }

    // A window on a desktop its screen does not show is refused: the engine
    // there would float it in the desktop in view, a context it is not in
    // (F1048, l13_bus_snap_hidden_multidesktop).
    void busSetWindowFloatingRefusesAWindowOnAHiddenDesktop()
    {
        FloatBackFixture f;
        FloatLoggingEngine tile;
        tile.activeScreens = {kRight};
        f.wta->setEngines(f.snap.get(), &tile, nullptr);
        const auto restore = qScopeGuard([&f] {
            f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
        });
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMinimized = false;
        meta.virtualDesktop = 2;
        f.registry.upsert(QStringLiteral("f1048-hidden"), meta);
        const QString w = QStringLiteral("app|f1048-hidden");
        f.registry.canonicalizeWindowId(w);
        tile.heldScreen.insert(w, kRight);
        f.setFrame(w, QRect(2100, 100, 800, 600));

        f.wta->setWindowFloating(w, true);
        QVERIFY2(tile.floatCalls.isEmpty(), "desktop 2 is not the one in view");

        f.layouts->setCurrentVirtualDesktop(2);
        f.wta->setWindowFloating(w, true);
        QCOMPARE(tile.floatCalls.size(), 1);
    }

    // So is a window on another activity than the one in view (F1048).
    void busSetWindowFloatingRefusesAWindowOnAnotherActivity()
    {
        FloatBackFixture f;
        FloatLoggingEngine tile;
        tile.activeScreens = {kRight};
        f.wta->setEngines(f.snap.get(), &tile, nullptr);
        const auto restore = qScopeGuard([&f] {
            f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
        });
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMinimized = false;
        meta.virtualDesktop = 1;
        meta.activity = QStringLiteral("act-2");
        f.registry.upsert(QStringLiteral("f1048-activity"), meta);
        const QString w = QStringLiteral("app|f1048-activity");
        f.registry.canonicalizeWindowId(w);
        tile.heldScreen.insert(w, kRight);
        f.setFrame(w, QRect(2100, 100, 800, 600));
        f.layouts->setCurrentActivity(QStringLiteral("act-1"));

        f.wta->setWindowFloating(w, true);
        QVERIFY2(tile.floatCalls.isEmpty(), "act-2 is not the activity in view");

        f.layouts->setCurrentActivity(QStringLiteral("act-2"));
        f.wta->setWindowFloating(w, true);
        QCOMPARE(tile.floatCalls.size(), 1);
    }

    // And a window in a context the user disabled PlasmaZones in (F1048).
    void busSetWindowFloatingRefusesADisabledContext()
    {
        FloatBackFixture f;
        FloatLoggingEngine tile;
        tile.activeScreens = {kRight};
        PlasmaZones::TestHelpers::ScreenDisablingResolver resolver;
        resolver.m_disabled.insert(kRight);
        f.wta->setEngines(f.snap.get(), &tile, nullptr);
        f.wta->setContextResolver(&resolver);
        const auto restore = qScopeGuard([&f] {
            f.wta->setContextResolver(nullptr);
            f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
        });
        const QString w = f.registerWindow(QStringLiteral("f1048-disabled"));
        tile.heldScreen.insert(w, kRight);
        f.setFrame(w, QRect(2100, 100, 800, 600));

        f.wta->setWindowFloating(w, true);
        QVERIFY2(tile.floatCalls.isEmpty(), "DP-2 is disabled");

        resolver.m_disabled.clear();
        f.wta->setWindowFloating(w, true);
        QCOMPARE(tile.floatCalls.size(), 1);
    }

    // The screen form holds a bus caller other than the compositor bridge,
    // such as the shell's placement map drawing a desktop not in view, to the
    // same refusal. The bridge's own calls are not held (F1108). The adaptor
    // is exported through a host that carries the call's context, as Daemon
    // does.
    void busSetWindowFloatingForScreenRefusesAHiddenWindowForAnotherCaller()
    {
        FloatBackFixture f;
        FloatLoggingEngine tile;
        tile.activeScreens = {kRight};
        BusHost host;
        auto* wta =
            new WindowTrackingAdaptor(f.layouts, &f.detector, f.screenMgr.get(), &f.settings, nullptr, nullptr, &host);
        wta->setWindowRegistry(&f.registry);
        wta->setEngines(nullptr, &tile, nullptr);
        const auto detach = qScopeGuard([wta] {
            wta->setEngines(nullptr, nullptr, nullptr);
            wta->setWindowRegistry(nullptr);
        });
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.virtualDesktop = 2;
        f.registry.upsert(QStringLiteral("f1108-hidden"), meta);
        const QString w = QStringLiteral("app|f1108-hidden");
        f.registry.canonicalizeWindowId(w);
        tile.heldScreen.insert(w, kRight);

        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        const QString path = QStringLiteral("/TestWtaF1108");
        const QString peerName = QStringLiteral("f1108-peer");
        QVERIFY(bus.registerObject(path, &host));
        QDBusConnection peer = QDBusConnection::connectToBus(QDBusConnection::SessionBus, peerName);
        const auto unregister = qScopeGuard([&bus, &path, &peerName] {
            bus.unregisterObject(path);
            QDBusConnection::disconnectFromBus(peerName);
        });
        QVERIFY(peer.isConnected());
        QDBusMessage call =
            QDBusMessage::createMethodCall(bus.baseService(), path, QStringLiteral("org.plasmazones.WindowTracking"),
                                           QStringLiteral("setWindowFloatingForScreen"));
        call << w << kRight << true;

        QDBusPendingCall refused = peer.asyncCall(call, 5000);
        QTRY_VERIFY(refused.isFinished());
        QVERIFY(!refused.isError());
        QVERIFY2(tile.floatCalls.isEmpty(), "another caller may not float a window on a hidden desktop");

        wta->setBridgeCallerCheck([&peer](const QString& busName) {
            return busName == peer.baseService();
        });
        QDBusPendingCall bridged = peer.asyncCall(call, 5000);
        QTRY_VERIFY(bridged.isFinished());
        QVERIFY(!bridged.isError());
        QCOMPARE(tile.floatCalls.size(), 1);
    }

    // On a snapping monitor the verb takes the window out of its zone, as a
    // float toggle does, rather than setting a float bit beside the zone. A
    // window the daemon has no record of is refused (F678).
    void busSetWindowFloatingTakesASnappedWindowOutOfItsZone()
    {
        FloatBackFixture f;
        const QString stranger = QStringLiteral("app|f678-unregistered");
        f.snap->commitSnap(stranger, f.zone(0), kLeft);
        f.wta->setWindowFloating(stranger, true);
        QVERIFY2(!f.snap->isFloating(stranger), "a window with no registry record is refused");

        const QString w = f.registerWindow(QStringLiteral("f678-snap"));
        f.snap->commitSnap(w, f.zone(1), kLeft);
        f.setFrame(w, f.zoneRect(1, kLeft));

        f.wta->setWindowFloating(w, true);

        QVERIFY(f.snap->isFloating(w));
        QVERIFY2(f.wta->service()->zoneForWindow(w).isEmpty(), "the float leaves the zone");
        QCOMPARE(f.wta->service()->preFloatZone(w), f.zone(1));
    }

    // The in-process sync names the snap zone only where snapping runs: a
    // frozen snap zone sent for a window on a tiling monitor lands in the
    // effect's zone cache and makes the tile read as snapped (F488).
    void syncWindowFloatingSendsNoZoneOffSnapping()
    {
        FloatBackFixture f;
        const QString onSnap = f.registerWindow(QStringLiteral("f488-snap"));
        const QString onTile = f.registerWindow(QStringLiteral("f488-tile"));
        f.snap->commitSnap(onSnap, f.zone(0), kLeft);
        f.snap->commitSnap(onTile, f.zone(1), kRight);
        f.snap->setLiveModeResolver([](const QString& screen) {
            return screen == kRight ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                    : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        const auto unset = qScopeGuard([&f] {
            f.snap->setLiveModeResolver({});
        });
        QSignalSpy stateSpy(f.wta, &WindowTrackingAdaptor::windowStateChanged);
        const auto floatedZone = [&stateSpy](const QString& windowId) -> std::optional<QString> {
            for (const QList<QVariant>& args : stateSpy) {
                const auto entry = args.at(1).value<PhosphorProtocol::WindowStateEntry>();
                if (args.at(0).toString() == windowId && entry.changeType == QLatin1String("floated")) {
                    return entry.zoneId;
                }
            }
            return std::nullopt;
        };

        f.wta->syncWindowFloating(onSnap, true);
        f.wta->syncWindowFloating(onTile, true);

        QCOMPARE(floatedZone(onSnap), std::optional<QString>(f.zone(0)));
        QCOMPARE(floatedZone(onTile), std::optional<QString>(QString()));
    }

private:
    /// A stub tiling engine that logs every setWindowFloat it is asked for.
    struct FloatLoggingEngine : StubPlacementEngine
    {
        struct Call
        {
            QString windowId;
            bool floating = false;
            QString screenId;
            bool operator==(const Call&) const = default;
        };
        QList<Call> floatCalls;
        void setWindowFloat(const QString& windowId, bool floating, const QString& screenId) override
        {
            floatCalls.append({windowId, floating, screenId});
        }
    };

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

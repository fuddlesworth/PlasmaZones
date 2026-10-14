// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_free_geometry_screens.cpp
 * @brief A remembered floating position is judged by its own monitor (F219, F830).
 *
 * Two outputs side by side: DP-1 2560x1440 at the origin and DP-2 1920x1080
 * to its right, staged headlessly through FakePhysicalScreenSource. The
 * float-back read used to finish with the generic rescue, which measures each
 * virtual screen with a flat 100 px floor and re-centres by the nearest
 * screen centre. A window smaller than the floor, or one across a
 * virtual-screen edge, read as off-screen and was clamped into one half or
 * onto the neighbouring monitor.
 *
 * The same spots are kept relative to their monitor (F814): they follow an
 * output that moves, is re-plugged elsewhere or changes mode, and the session
 * file stores them relative so a login with the monitors rearranged finds them
 * there. An EDID twin's identifier flip renames them in place (F189).
 */

#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QTest>
#include <QUuid>
#include <memory>
#include <optional>

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "FakePhysicalScreenSource.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kLeft = QStringLiteral("DP-1");
const QString kRight = QStringLiteral("DP-2");
const QRect kLeftGeometry(0, 0, 2560, 1440);
const QRect kRightGeometry(2560, 0, 1920, 1080);
const QString kWindow = QStringLiteral("app|float-back");
} // namespace

class TestWtsFreeGeometryScreens : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager.reset(PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        m_source = std::make_unique<PhosphorScreens::FakePhysicalScreenSource>();
        m_source->addScreen(kLeft, kLeftGeometry);
        m_source->addScreen(kRight, kRightGeometry);
        m_manager = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = m_source.get(), .useGeometrySensors = false});
        m_manager->start();
        m_service =
            std::make_unique<PhosphorPlacement::WindowTrackingService>(m_layoutManager.get(), m_manager.get(), nullptr);
        m_snapState = std::make_unique<PhosphorSnapEngine::SnapState>(QString(), nullptr);
        m_service->setSnapState(m_snapState.get());
    }

    void cleanup()
    {
        m_service->setSnapState(nullptr);
        m_service.reset();
        m_snapState.reset();
        m_manager.reset();
        m_source.reset();
        m_layoutManager.reset();
        m_guard.reset();
    }

    // An 80x60 palette near DP-1's right edge passes its own screen's overlap
    // test but not the flat floor, and the centre-distance rescue picked DP-2
    // and moved it there.
    void smallFloatBackStaysOnItsMonitor()
    {
        const QRect spot(2360, 20, 80, 60);
        seedFloatBack(kLeft, kLeft, spot);
        const auto back = m_service->validatedUnmanagedGeometry(kWindow, kLeft);
        QVERIFY(back.has_value());
        QCOMPARE(*back, spot);
    }

    // A 150x150 window across the middle of a split DP-1 holds less than the
    // floor of either half; the rescue clamped it into one of them.
    void floatBackAcrossAVirtualScreenEdgeIsKept()
    {
        const QString vs0 = splitLeftScreen();
        const QRect spot(1205, 600, 150, 150);
        seedFloatBack(vs0, vs0, spot);
        const auto back = m_service->validatedUnmanagedGeometry(kWindow, vs0);
        QVERIFY(back.has_value());
        QCOMPARE(*back, spot);
    }

    // The rescue itself picks the screen nearest the window's centre by its
    // edges, not by the distance between centres: this rect is 41 px from
    // DP-1's edge and 221 px from DP-2's, while DP-2's centre is the closer.
    void rescueTakesTheNearerEdge()
    {
        const auto rescued = m_service->validateGeometryForScreen(QRect(2525, 1225, 150, 150), {}, {});
        QVERIFY(rescued.has_value());
        QVERIFY2(kLeftGeometry.contains(*rescued), "the rescue clamps onto DP-1, whose edge is nearer");
        QCOMPARE(rescued->size(), QSize(150, 150));
    }

    // F830: the overlap thresholds are half the window's own extent, capped
    // at the floor, and a virtual key is measured against its output.
    void overlap_smallRectWhollyOnScreenIsStored()
    {
        QVERIFY(storedUnder(kLeft, QRect(100, 100, 80, 60)));
    }

    void overlap_halfOverEdgeIsStored()
    {
        QVERIFY(storedUnder(kLeft, QRect(2530, 100, 60, 60)));
    }

    void overlap_mostlyOffIsRefused()
    {
        QVERIFY(!storedUnder(kLeft, QRect(2540, 100, 80, 60)));
    }

    void overlap_virtualKeyWidensToOutput()
    {
        const QString vs0 = splitLeftScreen();
        // 30 px inside vs:0, the rest in vs:1 of the same output.
        QVERIFY(storedUnder(vs0, QRect(1250, 100, 400, 300)));
    }

    // F814: a remembered floating spot is kept relative to its monitor. DP-2
    // moves from DP-1's right to below it; the spot comes along. Without the
    // move handler the old absolute rect no longer lies on DP-2 and the read
    // refuses it.
    void outputMove_translatesFloatBack()
    {
        const QRect spot(2600, 100, 400, 300);
        seedFloatBack(kRight, kRight, spot);
        m_source->moveScreen(kRight, QRect(0, 1440, 1920, 1080));
        const auto back = m_service->validatedUnmanagedGeometry(kWindow, kRight);
        QVERIFY(back.has_value());
        QCOMPARE(*back, spot.translated(-2560, 1440));
    }

    void outputMove_translatesVirtualKeys()
    {
        const QString vs0 = splitLeftScreen();
        const QRect spot(300, 200, 500, 400);
        seedFloatBack(vs0, vs0, spot);
        m_source->moveScreen(kLeft, QRect(0, 1080, 2560, 1440));
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryByScreen.value(vs0), spot.translated(0, 1080));
    }

    // An output that is unplugged keeps its last geometry, so plugging it back
    // in somewhere else still moves its spots by the difference.
    void outputReplugElsewhere_translatesFloatBack()
    {
        const QRect spot(2600, 100, 400, 300);
        seedFloatBack(kRight, kRight, spot);
        m_source->removeScreen(kRight);
        m_source->addScreen(kRight, QRect(-1920, 0, 1920, 1080));
        const auto rec = m_service->placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryByScreen.value(kRight), spot.translated(-4480, 0));
    }

    // A smaller mode leaves a spot still on the output alone and pulls one it
    // left off back onto it.
    void outputResize_pullsOnlyAnOffSpotBack()
    {
        const QRect kept(2600, 50, 300, 200);
        const QRect off(4260, 900, 200, 150);
        seedFloatBack(kRight, kRight, kept);
        PhosphorEngine::WindowPlacement other;
        other.windowId = QStringLiteral("app|other");
        other.appId = QStringLiteral("app");
        other.screenId = kRight;
        other.freeGeometryByScreen.insert(kRight, off);
        QVERIFY(m_service->placementStore().record(other));
        const QRect smaller(2560, 0, 1280, 720);
        m_source->moveScreen(kRight, smaller);
        QCOMPARE(m_service->placementStore().peekExact(kWindow)->freeGeometryByScreen.value(kRight), kept);
        const QRect pulled = m_service->placementStore().peekExact(other.windowId)->freeGeometryByScreen.value(kRight);
        QCOMPARE(pulled.size(), off.size());
        QVERIFY2(smaller.contains(pulled), "the spot the smaller mode left off is back on the output");
    }

    // A spot the smaller mode leaves only partly on the output stays where it
    // was: part of the window is still on screen, and only a spot with nothing
    // left on the output is pulled back. FAILS if the resize clamps every spot.
    void outputResize_leavesAPartlyVisibleSpot()
    {
        const QRect straddling(3700, 600, 300, 200);
        seedFloatBack(kRight, kRight, straddling);
        const QRect smaller(2560, 0, 1280, 720);
        m_source->moveScreen(kRight, smaller);
        QCOMPARE(m_service->placementStore().peekExact(kWindow)->freeGeometryByScreen.value(kRight), straddling);
    }

    // A stale entry (filed before its monitor moved, so off the screen now)
    // no longer blocks a first capture, which first-capture-wins used to keep.
    void staleEntryDoesNotBlockFirstCapture()
    {
        seedFloatBack(kRight, kRight, QRect(100, 100, 300, 200));
        const QRect fresh(2700, 200, 300, 200);
        m_service->recordFreeGeometry(kWindow, kRight, fresh, /*overwrite=*/false);
        QCOMPARE(m_service->placementStore().peekExact(kWindow)->freeGeometryByScreen.value(kRight), fresh);
    }

    // The login half: the session file holds the spot relative to DP-2, so a
    // login with DP-2 below DP-1 finds it on DP-2. Written absolute, it read
    // back at its old coordinates, off the monitor.
    void savedSpotSurvivesARearrangedLogin()
    {
        const QRect spot(2600, 100, 400, 300);
        seedFloatBack(kRight, kRight, spot);
        const QJsonObject saved = m_service->placementStore().serialize({}, originsOf(m_service.get()));
        QCOMPARE(saved.value(QStringLiteral("_version")).toInt(), PhosphorEngine::WindowPlacementStore::FormatVersion);

        m_source->moveScreen(kRight, QRect(0, 1440, 1920, 1080));
        PhosphorPlacement::WindowTrackingService login(m_layoutManager.get(), m_manager.get(), nullptr);
        login.placementStore().deserialize(saved, originsOf(&login));
        const auto rec = login.placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryByScreen.value(kRight), QRect(40, 1540, 400, 300));
    }

    // A file from before the format bump holds absolute rects. The one-time
    // migration keeps a spot on a monitor connected now where it is and drops
    // one on a monitor that is not, since no frame for it is known.
    void unversionedFileIsMigratedOnce()
    {
        const QRect spot(2600, 100, 400, 300);
        seedFloatBack(kRight, kRight, spot);
        QJsonObject legacy;
        {
            PhosphorEngine::WindowPlacementStore absolute;
            PhosphorEngine::WindowPlacement rec = *m_service->placementStore().peekExact(kWindow);
            rec.freeGeometryByScreen.insert(QStringLiteral("HDMI-9"), QRect(9000, 0, 300, 200));
            QVERIFY(absolute.record(rec));
            legacy = absolute.serialize();
            legacy.remove(QStringLiteral("_version"));
        }
        PhosphorPlacement::WindowTrackingService login(m_layoutManager.get(), m_manager.get(), nullptr);
        login.placementStore().deserialize(legacy, originsOf(&login));
        const auto rec = login.placementStore().peekExact(kWindow);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->freeGeometryByScreen.value(kRight), spot);
        QVERIFY(!rec->freeGeometryByScreen.contains(QStringLiteral("HDMI-9")));
    }

    // F189: an EDID twin's identifier flip renames every stored id of the
    // monitor in place, its virtual-screen keys and the snap store's value
    // included, and the output's origin follows.
    void identifierFlipRenamesStoredIds()
    {
        const QString twin = QStringLiteral("DP-2/twin");
        const QRect spot(2600, 100, 400, 300);
        seedFloatBack(kRight, kRight, spot);
        PhosphorEngine::WindowPlacement rec = *m_service->placementStore().peekExact(kWindow);
        rec.freeGeometryByScreen.insert(PhosphorIdentity::VirtualScreenId::make(kRight, 1), spot);
        QVERIFY(m_service->placementStore().record(rec));
        const QString snapped = QStringLiteral("app|snapped");
        m_snapState->assignWindowToZones(snapped, {QUuid::createUuid().toString()}, kRight, 1);

        Q_EMIT m_manager->screenIdentifierChanged(kRight, twin);

        const auto after = m_service->placementStore().peekExact(kWindow);
        QVERIFY(after.has_value());
        QCOMPARE(after->screenId, twin);
        QCOMPARE(after->freeGeometryByScreen.value(twin), spot);
        QCOMPARE(after->freeGeometryByScreen.value(PhosphorIdentity::VirtualScreenId::make(twin, 1)), spot);
        QVERIFY(!after->freeGeometryByScreen.contains(kRight));
        QCOMPARE(m_snapState->screenForWindow(snapped), twin);
        const std::optional<QPoint> origin = m_service->outputOrigin(twin);
        QVERIFY(origin.has_value());
        QCOMPARE(*origin, kRightGeometry.topLeft());
    }

private:
    static PhosphorEngine::WindowPlacementStore::OutputOrigin originsOf(PhosphorPlacement::WindowTrackingService* svc)
    {
        return [svc](const QString& screenId) {
            return svc->outputOrigin(screenId);
        };
    }

    /// Split DP-1 50/50 side by side and answer its left half's id.
    QString splitLeftScreen()
    {
        const auto makeDef = [](int index, const QRectF& region) {
            PhosphorScreens::VirtualScreenDef def;
            def.id = PhosphorIdentity::VirtualScreenId::make(kLeft, index);
            def.physicalScreenId = kLeft;
            def.displayName = QStringLiteral("Half %1").arg(index);
            def.region = region;
            def.index = index;
            return def;
        };
        PhosphorScreens::VirtualScreenConfig config;
        config.physicalScreenId = kLeft;
        config.screens.append(makeDef(0, QRectF(0, 0, 0.5, 1.0)));
        config.screens.append(makeDef(1, QRectF(0.5, 0, 0.5, 1.0)));
        if (!m_manager->setVirtualScreenConfig(kLeft, config)) {
            qFatal("could not split DP-1");
        }
        return PhosphorIdentity::VirtualScreenId::make(kLeft, 0);
    }

    /// A record written straight into the store, the way one loads from disk.
    void seedFloatBack(const QString& recordScreen, const QString& key, const QRect& spot)
    {
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = kWindow;
        rec.appId = QStringLiteral("app");
        rec.screenId = recordScreen;
        rec.freeGeometryByScreen.insert(key, spot);
        QVERIFY(m_service->placementStore().record(rec));
    }

    /// Whether the float-back writer files @p rect under @p screenId.
    bool storedUnder(const QString& screenId, const QRect& rect)
    {
        m_service->recordFreeGeometry(kWindow, screenId, rect, /*overwrite=*/true);
        const auto rec = m_service->placementStore().peekExact(kWindow);
        return rec && rec->freeGeometryByScreen.value(screenId) == rect;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutManager;
    std::unique_ptr<PhosphorScreens::FakePhysicalScreenSource> m_source;
    std::unique_ptr<PhosphorScreens::ScreenManager> m_manager;
    std::unique_ptr<PhosphorSnapEngine::SnapState> m_snapState;
    std::unique_ptr<PhosphorPlacement::WindowTrackingService> m_service;
};

QTEST_MAIN(TestWtsFreeGeometryScreens)
#include "test_wts_free_geometry_screens.moc"

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
 */

#include <QRect>
#include <QRectF>
#include <QString>
#include <QTest>
#include <memory>

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

private:
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

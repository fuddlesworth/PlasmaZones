// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file wta_float_back_fixture.h
 * @brief Shared fixture for the float-back tests (test_wta_float_back_model.cpp
 *        and test_wta_presnap_capture.cpp): two outputs side by side on a real
 *        ScreenManager, a snap engine on per-screen stores, a stub tiling
 *        engine in the autotile slot and a window registry.
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
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
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

inline const QString kLeft = QStringLiteral("DP-1");
inline const QString kRight = QStringLiteral("DP-2");
inline const QRect kLeftRect(0, 0, 1920, 1080);
inline const QRect kRightRect(1920, 0, 1920, 1080);

struct FloatBackFixture
{
    /// @p vdm (optional, not owned) reaches the adaptor and the snap engine.
    explicit FloatBackFixture(PhosphorWorkspaces::VirtualDesktopManager* vdm = nullptr)
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
        wta = new WindowTrackingAdaptor(layouts, &detector, screenMgr.get(), &settings, vdm, nullptr, &parent);
        wta->setWindowRegistry(&registry);
        snap = std::make_unique<PhosphorSnapEngine::SnapEngine>(layouts, wta->service(), &detector, vdm, nullptr);
        snap->setEngineSettings(&settings);
        snap->setWindowRegistry(&registry);
        snap->setNavigationStateProvider(wta);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap.get());
        wta->service()->setSnapStateResolver(PhosphorPlacement::snapStateResolverFor(snap.get()));
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_desktop_reconcile.cpp
 *
 * Pins the daemon-side desktop-membership reconcile (#1076 and its family):
 * when the compositor reports a window's virtual-desktop set changed, the
 * TilingAdaptor releases the window from any engine state keyed by a
 * desktop the window no longer belongs to, whatever desktop the screen is
 * showing at the time. The effect used to own that decision from proxies of
 * the desktop in view, and every (moved window, desktop in view, mode per
 * desktop) combination it had not foreseen left a slot behind.
 *
 * Driven end to end through a real WindowRegistry, so the subscription
 * setWindowRegistry makes is under test too, against a real AutotileEngine
 * and a real ScrollEngine with headless geometry.
 */

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "dbus/tilingadaptor/tilingadaptor.h"
#include "helpers/AutotileTestHelpers.h"

using namespace PlasmaZones;

namespace {
const QString kScreen = QStringLiteral("DP-1");
const QString kInstance = QStringLiteral("11111111-2222-3333-4444-555555555555");
const QString kWindow = QStringLiteral("app|11111111-2222-3333-4444-555555555555");
const QString kStays = QStringLiteral("app|66666666-7777-8888-9999-000000000000");

PhosphorEngine::WindowMetadata onDesktop(int desktop, const QList<int>& span = {})
{
    PhosphorEngine::WindowMetadata meta;
    meta.appId = QStringLiteral("app");
    meta.title = QStringLiteral("t");
    meta.virtualDesktop = desktop;
    meta.virtualDesktops = span;
    return meta;
}
} // namespace

class TestTilingAdaptorDesktopReconcile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // The reported bug: the user is looking at desktop 3, where the screen
    // has nothing assigned, and drags a window there from tiled desktop 1.
    void autotile_windowMovedToUnassignedDesktop_isReleasedFromSourceState()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorTileEngine::AutotileEngine engine(nullptr, nullptr, nullptr, TestHelpers::testRegistry());
        engine.setWindowRegistry(&registry);
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));

        engine.setCurrentDesktopForScreen(kScreen, 1);
        engine.setAutotileScreens({kScreen});
        engine.windowOpened(kWindow, kScreen);
        engine.windowOpened(kStays, kScreen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(kScreen);
        QVERIFY(d1 != nullptr);
        QCOMPARE(d1->windowCount(), 2);

        // Desktop 3 in view, unassigned: the screen leaves the managed set.
        engine.setCurrentDesktopForScreen(kScreen, 3);
        engine.setAutotileScreens({});
        QVERIFY(!engine.isAutotileScreen(kScreen));

        // KWin reports the move; the effect's metadata push lands in the registry.
        registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY(!engine.isWindowTracked(kWindow));
        QVERIFY(!d1->containsWindow(kWindow));
        QVERIFY(d1->containsWindow(kStays));
        QCOMPARE(d1->windowCount(), 1);
    }

    // The mirror move onto ANOTHER tiled desktop releases the source state
    // too; adoption on the destination is the effect's, and is not run here.
    void autotile_windowMovedBetweenTiledDesktops_isReleasedFromSourceState()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorTileEngine::AutotileEngine engine(nullptr, nullptr, nullptr, TestHelpers::testRegistry());
        engine.setWindowRegistry(&registry);
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));
        engine.setCurrentDesktopForScreen(kScreen, 1);
        engine.setAutotileScreens({kScreen});
        engine.windowOpened(kWindow, kScreen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(kScreen);
        QVERIFY(d1->containsWindow(kWindow));

        engine.setCurrentDesktopForScreen(kScreen, 2);
        engine.setAutotileScreens({kScreen});
        registry.upsert(kInstance, onDesktop(2));
        QCoreApplication::processEvents();

        QVERIFY(!d1->containsWindow(kWindow));
        QVERIFY(!engine.isWindowTracked(kWindow));
    }

    // A desktop set that merely GREW to include the holding desktop, and a
    // window that went sticky (desktop 0), both keep their slot.
    void autotile_spanGrowthAndSticky_keepTheSlot()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorTileEngine::AutotileEngine engine(nullptr, nullptr, nullptr, TestHelpers::testRegistry());
        engine.setWindowRegistry(&registry);
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));
        engine.setCurrentDesktopForScreen(kScreen, 1);
        engine.setAutotileScreens({kScreen});
        engine.windowOpened(kWindow, kScreen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(kScreen);
        QVERIFY(d1->containsWindow(kWindow));

        registry.upsert(kInstance, onDesktop(1, {1, 3}));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        registry.upsert(kInstance, onDesktop(0));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));

        // A title tick on a window that stays put is not a move either.
        PhosphorEngine::WindowMetadata retitled = onDesktop(1);
        retitled.title = QStringLiteral("t2");
        registry.upsert(kInstance, retitled);
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));
    }

    // A detached registry no longer drives the reconcile (the shutdown contract).
    void autotile_detachedRegistry_doesNothing()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorTileEngine::AutotileEngine engine(nullptr, nullptr, nullptr, TestHelpers::testRegistry());
        engine.setWindowRegistry(&registry);
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);
        adaptor.setWindowRegistry(nullptr);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));
        engine.setCurrentDesktopForScreen(kScreen, 1);
        engine.setAutotileScreens({kScreen});
        engine.windowOpened(kWindow, kScreen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(kScreen);

        registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();
        QVERIFY(d1->containsWindow(kWindow));
    }

    // Same invariant for the scrolling engine: a column left on desktop 1
    // when its window moved to an unassigned desktop 3 is released.
    void scrolling_windowMovedToUnassignedDesktop_isReleasedFromSourceStrip()
    {
        PhosphorEngine::WindowRegistry registry;
        PhosphorScrollEngine::ScrollEngine engine(nullptr, nullptr);
        engine.setWindowRegistry(&registry);
        engine.setScreenGeometryProviders(
            [](const QString&) {
                return QRect(0, 40, 1200, 760);
            },
            [](const QString&) {
                return QRect(0, 0, 1200, 800);
            });
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        adaptor.setLifecycleEngines({&engine});
        adaptor.setWindowRegistry(&registry);

        registry.canonicalizeWindowId(kWindow);
        registry.upsert(kInstance, onDesktop(1));
        engine.setCurrentDesktopForScreen(kScreen, 1);
        engine.setActiveScreens({kScreen});
        engine.windowOpened(kWindow, kScreen);
        engine.windowOpened(kStays, kScreen);
        QCoreApplication::processEvents();
        QVERIFY(engine.isWindowTracked(kWindow));
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine.heldKeyForWindow(kWindow);
        QVERIFY(held.has_value());
        QCOMPARE(held->desktop, 1);

        engine.setCurrentDesktopForScreen(kScreen, 3);
        engine.setActiveScreens({});
        QVERIFY(engine.isWindowTracked(kWindow));

        registry.upsert(kInstance, onDesktop(3));
        QCoreApplication::processEvents();

        QVERIFY(!engine.isWindowTracked(kWindow));
        QVERIFY(engine.isWindowTracked(kStays));
        QVERIFY(!engine.heldKeyForWindow(kWindow).has_value());
    }
};

QTEST_MAIN(TestTilingAdaptorDesktopReconcile)
#include "test_tiling_adaptor_desktop_reconcile.moc"

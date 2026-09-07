// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_per_screen_desktop.cpp
 * @brief Phase 3 of per-screen virtual desktops (#648): AutotileEngine's
 *        per-output desktop input (setCurrentDesktopForScreen).
 *
 * Plasma 6.7 "switch desktops independently for each screen" lets each output sit
 * on its own virtual desktop. The engine already keyed TilingState by
 * (screen, desktop, activity); setCurrentDesktopForScreen feeds a per-screen
 * desktop into currentKeyForScreen() via m_screenCurrentDesktop (distinct from the
 * sticky-pin override map). It is a PURE context swap — switching a screen's
 * desktop must NOT migrate windows between per-desktop states.
 *
 * State keying is exercised through the public tilingStateForScreen() (the
 * underlying map / currentKeyForScreen are private), mirroring the existing
 * perDesktopState_* tests which drive the global setCurrentDesktop the same way.
 */

#include <QTest>

#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "helpers/AutotileTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

namespace {
const QString kS1 = QStringLiteral("DP-1");
const QString kS2 = QStringLiteral("DP-2");
} // namespace

class TestAutotilePerScreenDesktop : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A single screen on two different per-output desktops yields two distinct
    // TilingState instances (the desktop dimension of the key differs).
    void distinctStatesPerScreenDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d3 = engine.tilingStateForScreen(kS1);

        engine.setCurrentDesktopForScreen(kS1, 5);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d5 = engine.tilingStateForScreen(kS1);

        QVERIFY(d3 != nullptr);
        QVERIFY(d5 != nullptr);
        QVERIFY(d3 != d5);
    }

    // Switching a screen's desktop and back returns the ORIGINAL state instance —
    // proving setCurrentDesktopForScreen is a pure context swap with no migration:
    // the first desktop's state was preserved untouched while the screen was away.
    void pureContextSwapPreservesState()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d3 = engine.tilingStateForScreen(kS1);

        engine.setCurrentDesktopForScreen(kS1, 5);
        engine.setAutotileScreens({kS1});
        (void)engine.tilingStateForScreen(kS1); // materialise the desktop-5 state

        engine.setCurrentDesktopForScreen(kS1, 3); // swap back
        engine.setAutotileScreens({kS1});
        QCOMPARE(engine.tilingStateForScreen(kS1), d3);
    }

    // A per-screen desktop change touches neither the global current desktop nor
    // any other screen's resolved state.
    void isolatedFromGlobalAndOtherScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        engine.setAutotileScreens({kS1, kS2});
        PhosphorTiles::TilingState* s2before = engine.tilingStateForScreen(kS2);
        const int globalBefore = engine.currentDesktop();

        engine.setCurrentDesktopForScreen(kS1, 7); // change only kS1
        engine.setAutotileScreens({kS1, kS2});

        QCOMPARE(engine.currentDesktop(), globalBefore); // global untouched
        QCOMPARE(engine.tilingStateForScreen(kS2), s2before); // kS2 untouched
    }

    // clearCurrentDesktopForScreen reverts a screen to the global current desktop.
    void clearRevertsToGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        // Global default desktop is 1; capture the screen's global-desktop state.
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* globalState = engine.tilingStateForScreen(kS1);

        engine.setCurrentDesktopForScreen(kS1, 4);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d4 = engine.tilingStateForScreen(kS1);
        QVERIFY(d4 != globalState);

        engine.clearCurrentDesktopForScreen(kS1); // back to the global desktop
        engine.setAutotileScreens({kS1});
        QCOMPARE(engine.tilingStateForScreen(kS1), globalState);
    }

    // #1076: a window moved from a tiled desktop onto an unassigned desktop
    // (the screen runs no tiling there) is released by the effect while the
    // unassigned desktop is in view. The release must reach the SOURCE
    // desktop's state by window id even though the screen is not autotile in
    // the current context, otherwise the slot stays occupied on return.
    void releaseReachesSourceDesktopFromUnmanagedContext()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString win = QStringLiteral("win-moved");

        engine.setCurrentDesktopForScreen(kS1, 1);
        engine.setAutotileScreens({kS1});
        engine.windowOpened(win, kS1);
        engine.windowOpened(QStringLiteral("win-stays"), kS1);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(kS1);
        QVERIFY(d1 != nullptr);
        QVERIFY(d1->containsWindow(win));
        QCOMPARE(d1->windowCount(), 2);

        // The user switches the screen to desktop 3, where nothing is assigned.
        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({});
        QVERIFY(!engine.isAutotileScreen(kS1));
        QVERIFY(engine.isWindowTracked(win));

        // The effect's arrival arm releases the window from the desktop in view.
        engine.windowClosed(win);
        QCoreApplication::processEvents();
        QVERIFY(!engine.isWindowTracked(win));

        // Back on desktop 1 the stack holds only the window that stayed.
        engine.setCurrentDesktopForScreen(kS1, 1);
        engine.setAutotileScreens({kS1});
        QCOMPARE(engine.tilingStateForScreen(kS1), d1);
        QVERIFY(!d1->containsWindow(win));
        QVERIFY(d1->containsWindow(QStringLiteral("win-stays")));
        QCOMPARE(d1->windowCount(), 1);
    }
};

QTEST_MAIN(TestAutotilePerScreenDesktop)
#include "test_autotile_per_screen_desktop.moc"

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
};

QTEST_MAIN(TestAutotilePerScreenDesktop)
#include "test_autotile_per_screen_desktop.moc"

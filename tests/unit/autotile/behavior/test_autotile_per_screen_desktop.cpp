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

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "helpers/AutotileTestHelpers.h"

#include <optional>

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
    // (the screen runs no tiling there). The daemon's reconcile has to be able
    // to ASK which desktop still holds the window while the screen is not
    // autotile in the current context, and the release then has to reach that
    // desktop's state, otherwise the slot stays occupied on return.
    void heldKeyAndReleaseReachSourceDesktopFromUnmanagedContext()
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

        // The query the daemon's reconcile asks. It answers the BACKGROUND
        // desktop that still holds the window, while the current-context
        // predicate beside it correctly answers nothing — that contrast is the
        // whole difference between the two, and it is what lets the daemon
        // decide from a context that can say nothing about desktop 1.
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine.heldKeyForWindow(win);
        QVERIFY(held.has_value());
        QCOMPARE(held->desktop, 1);
        QCOMPARE(held->screenId, kS1);
        QVERIFY(engine.heldScreenForWindow(win).isEmpty());

        // The release the daemon then issues against that engine.
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

    // A window removed from a BACKGROUND desktop's state must not drag the
    // screen's CURRENT layout through a retile. retileScreen resolves the
    // current context, so the reflow would land on the wrong desktop and the
    // mutated one would go untouched either way.
    //
    // placementChanged is the discriminator, not state: without a ScreenManager
    // recalculateLayout is a structural no-op, so the desktop-2 state looks
    // identical whether or not it was retiled.
    void removalFromBackgroundDesktop_doesNotRetileTheCurrentOne()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString onD1 = QStringLiteral("win-d1");
        const QString onD2 = QStringLiteral("win-d2");

        engine.setCurrentDesktopForScreen(kS1, 1);
        engine.setAutotileScreens({kS1});
        engine.windowOpened(onD1, kS1);
        QCoreApplication::processEvents();

        engine.setCurrentDesktopForScreen(kS1, 2);
        engine.setAutotileScreens({kS1});
        engine.windowOpened(onD2, kS1);
        QCoreApplication::processEvents();

        QSignalSpy spy(&engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        QVERIFY(spy.isValid());

        // Desktop 1's window closes while desktop 2 is in view.
        engine.windowClosed(onD1);
        QCoreApplication::processEvents();
        QVERIFY(!engine.isWindowTracked(onD1));
        QCOMPARE(spy.count(), 0);

        // The control arm: closing the CURRENT context's window still retiles,
        // so the assertion above is a real gate rather than a dead spy.
        spy.clear();
        engine.windowClosed(onD2);
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    // Deleting a virtual desktop in the middle renumbers every desktop above
    // it. The engine's state has to move with the numbering, or every window on
    // a shifted desktop reads as having left it.
    void renumberAfterRemoval_shiftsStatesAboveTheRemovedDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString onD1 = QStringLiteral("win-d1");
        const QString onD2 = QStringLiteral("win-d2");
        const QString onD3 = QStringLiteral("win-d3");

        for (const auto& [desktop, windowId] : QList<std::pair<int, QString>>{{1, onD1}, {2, onD2}, {3, onD3}}) {
            engine.setCurrentDesktopForScreen(kS1, desktop);
            engine.setAutotileScreens({kS1});
            engine.windowOpened(windowId, kS1);
            QCoreApplication::processEvents();
        }
        QCOMPARE(engine.heldKeyForWindow(onD3)->desktop, 3);

        // Desktop 2 is deleted: its own state goes, and 3 becomes 2.
        engine.pruneStatesForDesktop(2);
        engine.renumberDesktopsAfterRemoval(2);

        QVERIFY(!engine.isWindowTracked(onD2));
        // Untouched below the removal.
        QVERIFY(engine.heldKeyForWindow(onD1).has_value());
        QCOMPARE(engine.heldKeyForWindow(onD1)->desktop, 1);
        // Shifted down one, and still tracked — this is the window the
        // desktop-membership reconcile would otherwise release.
        QVERIFY(engine.heldKeyForWindow(onD3).has_value());
        QCOMPARE(engine.heldKeyForWindow(onD3)->desktop, 2);
        QVERIFY(engine.isWindowTracked(onD3));
    }
};

QTEST_MAIN(TestAutotilePerScreenDesktop)
#include "test_autotile_per_screen_desktop.moc"

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
#include <QJsonObject>
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
        QVERIFY(engine.heldKeyForWindow(onD3).has_value());
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

    // The per-output desktop map has to move with the state keys, and exactly
    // ONCE. A renumber that shifts the tracker a second time — because
    // something re-pushed the screen's desktop in between — leaves every key
    // derived from it off by one, and a screen sitting just above the removal
    // lands on the removed number itself, where the prune erases its entry
    // outright and the screen falls back to the global desktop.
    void renumberAfterRemoval_shiftsThePerOutputDesktopExactlyOnce()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setCurrentDesktopForScreen(kS1, 4);
        engine.setCurrentDesktopForScreen(kS2, 3);
        engine.setAutotileScreens({kS1, kS2});
        engine.windowOpened(QStringLiteral("win-s1"), kS1);
        engine.windowOpened(QStringLiteral("win-s2"), kS2);
        QCoreApplication::processEvents();

        engine.pruneStatesForDesktop(2);
        engine.renumberDesktopsAfterRemoval(2);

        // 4 → 3 and 3 → 2, each moved once. A double shift would read 2 and 1.
        QVERIFY(engine.heldKeyForWindow(QStringLiteral("win-s1")).has_value());
        QCOMPARE(engine.heldKeyForWindow(QStringLiteral("win-s1"))->desktop, 3);
        QVERIFY(engine.heldKeyForWindow(QStringLiteral("win-s2")).has_value());
        QCOMPARE(engine.heldKeyForWindow(QStringLiteral("win-s2"))->desktop, 2);
        // And the screens still resolve to those same states, which is what
        // fails when the tracker and the state keys disagree: the tracker names
        // a desktop the states are not on, so the lookup mints a fresh empty
        // one there instead of finding the migrated stack.
        PhosphorTiles::TilingState* s1 = engine.tilingStateForScreen(kS1);
        PhosphorTiles::TilingState* s2 = engine.tilingStateForScreen(kS2);
        QVERIFY(s1 != nullptr);
        QVERIFY(s2 != nullptr);
        QVERIFY(s1->containsWindow(QStringLiteral("win-s1")));
        QVERIFY(s2->containsWindow(QStringLiteral("win-s2")));
    }

    // The script-state stash is keyed by context and OUTLIVES the state it was
    // written for — it is rescued as the state dies — so a renumber that moved
    // only the live states would leave a bag under the number its desktop had
    // before. The desktop that inherits that number would then be handed the
    // deleted desktop's layout, which is what the prune's own erase guards
    // against on the other path.
    void renumberAfterRemoval_shiftsAStashWithNoLiveState()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QJsonObject bag3{{QStringLiteral("marker"), QStringLiteral("desktop-3")}};
        const QJsonObject bag4{{QStringLiteral("marker"), QStringLiteral("desktop-4")}};

        // TWO bags, on adjacent desktops, so the ORDER of the shift matters. A
        // chain like this is what a descending walk gets wrong: it would try
        // desktop 4 first, find desktop 3 still occupied by the bag that has
        // not moved yet, and drop it.
        for (const auto& [desktop, bag] : QList<std::pair<int, QJsonObject>>{{3, bag3}, {4, bag4}}) {
            engine.setCurrentDesktopForScreen(kS1, desktop);
            engine.setAutotileScreens({kS1});
            engine.windowOpened(QStringLiteral("win-d%1").arg(desktop), kS1);
            QCoreApplication::processEvents();
            PhosphorTiles::TilingState* state = engine.tilingStateForScreen(kS1);
            QVERIFY(state != nullptr);
            state->setScriptState(bag);
            // Taking the screen out of the set tears the state down and rescues
            // the bag, leaving the desktop holding a stash and NO state — the
            // shape the stateless shift exists for.
            engine.setAutotileScreens({});
            QCoreApplication::processEvents();
        }

        engine.pruneStatesForDesktop(2);
        engine.renumberDesktopsAfterRemoval(2);

        // 3 became 2 and 4 became 3. Each must be findable at its new number,
        // which only happens if both moved and neither was dropped.
        engine.setCurrentDesktopForScreen(kS1, 2);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(kS1);
        QVERIFY(d2 != nullptr);
        QCOMPARE(d2->scriptState(), bag3);

        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d3 = engine.tilingStateForScreen(kS1);
        QVERIFY(d3 != nullptr);
        QCOMPARE(d3->scriptState(), bag4);
    }

    // The two halves of the renumber meeting on one key. A LIVE state migrating
    // down lands on a key a STATELESS bag already occupies, and the state
    // brings its own bag with it. The stateless pass runs afterwards and must
    // not then pick up the live state's bag and carry it off: that would part a
    // live layout from its script state AND orphan the bag on a key nothing
    // lives on, which is both failures the shift exists to prevent, caused by
    // the shift itself.
    void renumberAfterRemoval_liveStateBagIsNotStolenByTheStatelessPass()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QJsonObject staleBag{{QStringLiteral("marker"), QStringLiteral("stateless-d3")}};
        const QJsonObject liveBag{{QStringLiteral("marker"), QStringLiteral("live-d4")}};

        // Desktop 3: a state that dies, leaving its bag behind with no state.
        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({kS1});
        engine.windowOpened(QStringLiteral("win-d3"), kS1);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d3 = engine.tilingStateForScreen(kS1);
        QVERIFY(d3 != nullptr);
        d3->setScriptState(staleBag);
        engine.setAutotileScreens({});
        QCoreApplication::processEvents();

        // Desktop 4: a state that STAYS live, with its own bag.
        engine.setCurrentDesktopForScreen(kS1, 4);
        engine.setAutotileScreens({kS1});
        engine.windowOpened(QStringLiteral("win-d4"), kS1);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d4 = engine.tilingStateForScreen(kS1);
        QVERIFY(d4 != nullptr);
        d4->setScriptState(liveBag);

        // Remove desktop 2. Desktop 3's stateless bag targets 2; desktop 4's
        // live state targets 3, which is where that stale bag sits right now.
        engine.pruneStatesForDesktop(2);
        engine.renumberDesktopsAfterRemoval(2);

        // The live state is on desktop 3 now.
        engine.setCurrentDesktopForScreen(kS1, 3);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* moved = engine.tilingStateForScreen(kS1);
        QVERIFY(moved != nullptr);
        QVERIFY2(moved->containsWindow(QStringLiteral("win-d4")),
                 "the live desktop-4 state should have migrated down to desktop 3");

        // The discriminator is desktop 2, and it has to be read through a
        // freshly CREATED state: a migrated state carries its script bag in the
        // object itself, so asking the live one proves nothing either way. What
        // desktop 2 restores is whichever bag the shift left on its key. The
        // stale desktop-3 leftover is the right answer; the live state's own
        // bag being there means the stateless pass stole it after the state
        // loop moved it in.
        engine.setCurrentDesktopForScreen(kS1, 2);
        engine.setAutotileScreens({kS1});
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(kS1);
        QVERIFY(d2 != nullptr);
        QVERIFY2(d2->scriptState() != liveBag, "the live state's bag must not have been carried onto desktop 2");
        QCOMPARE(d2->scriptState(), staleBag);
    }
};

QTEST_MAIN(TestAutotilePerScreenDesktop)
#include "test_autotile_per_screen_desktop.moc"

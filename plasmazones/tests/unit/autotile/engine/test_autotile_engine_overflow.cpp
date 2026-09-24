// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/types/constants.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief AutotileEngine tests for overflow window management
 */
class TestAutotileEngineOverflow : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        PlasmaZones::TestHelpers::testRegistry();
    }

    void testOverflow_excessWindowsAutoFloated()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state != nullptr);
        QCOMPARE(state->tiledWindowCount(), 3);

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 500), QRect(500, 0, 500, 500)});
        engine.retile(screenName);

        QCOMPARE(state->tiledWindowCount(), 2);
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
    }

    // Discussion #1028: a window OPENING while the tiled set is already at
    // maxWindows used to be refused outright by onWindowAdded — left untracked
    // and unfloated, but with the reverse-map key windowOpened had already
    // written still in place. That phantom key made isWindowTracked answer true
    // for a window no TilingState held, so the daemon drove float traffic
    // straight into a state that silently ignored it: the minimize float and
    // the unminimize unfloat both became no-ops for the rest of the session.
    void testOverflow_windowOpenedPastCapIsTrackedAndFloated()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 2;

        engine.setAutotileScreens({screenName});

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state != nullptr);
        // Zones deliberately NOT seeded. No algorithm is wired in this harness,
        // so recalculateLayout bails and the zone vector stays empty — which is
        // exactly the branch this test exists for: applyTiling's overflow pass
        // now runs off the CAP, above the zones-empty bail, rather than off the
        // calculated zone count. Seeding zones here would steer the run onto
        // the pre-existing non-empty path and never touch that branch.

        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        QVERIFY(state->containsWindow(QStringLiteral("win-3")));
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
        QCOMPARE(state->tiledWindowCount(), 2);
        // The SYMPTOM, not just the mechanism. containsWindow asks the state;
        // isWindowTracked asks the reverse map, and it was the divergence
        // between those two that #1028 actually was — the daemon reads the
        // reverse map to route float traffic. A regression that re-diverges
        // them in either direction has to fail here.
        QVERIFY(engine.isWindowTracked(QStringLiteral("win-3")));

        // An unfloat with nowhere to land is REFUSED, not performed and then
        // silently undone. This is the minimize/unminimize edge the daemon
        // drives through setWindowFloat: the screen is still at cap, so the
        // window has no slot to return to and stays floating. Before the
        // refusal the engine unfloated, retiled synchronously, and the overflow
        // pass re-floated the window inside the same call — so the caller read
        // back "still floating", retried, and burned its whole retry budget on
        // a request that could never converge.
        //
        // Asserting the INTERMEDIATE state is what makes this discriminating.
        // Checking only the end state passes when both calls are silent no-ops,
        // which is exactly the #1028 defect.
        engine.unfloatWindow(QStringLiteral("win-3"));
        QCoreApplication::processEvents();
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
        QCOMPARE(state->tiledWindowCount(), 2);

        // With room, the same call succeeds — the refusal is about capacity,
        // not about this window.
        engine.config()->maxWindows = 3;
        engine.unfloatWindow(QStringLiteral("win-3"));
        QCoreApplication::processEvents();
        QVERIFY(!state->isFloating(QStringLiteral("win-3")));
        QCOMPARE(state->tiledWindowCount(), 3);

        engine.floatWindow(QStringLiteral("win-3"));
        QCoreApplication::processEvents();
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
        QVERIFY(engine.isWindowTracked(QStringLiteral("win-3")));
    }

    void testOverflow_emitsFloatingSignal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-a"), screenName);
        engine.windowOpened(QStringLiteral("win-b"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});

        // Overflow windows are now emitted via windowsBatchFloated (batched)
        // instead of per-window windowFloatingChanged signals.
        QSignalSpy batchFloatSpy(&engine, &AutotileEngine::windowsBatchFloated);
        engine.retile(screenName);

        bool foundOverflow = false;
        for (int i = 0; i < batchFloatSpy.count(); ++i) {
            QStringList windowIds = batchFloatSpy.at(i).at(0).toStringList();
            if (windowIds.contains(QStringLiteral("win-b"))) {
                foundOverflow = true;
                break;
            }
        }
        QVERIFY(foundOverflow);
    }

    void testOverflow_unfloatWhenRoomAvailable()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QCOMPARE(state->tiledWindowCount(), 3);

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 500), QRect(500, 0, 500, 500)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
        QCOMPARE(state->tiledWindowCount(), 2);

        engine.windowClosed(QStringLiteral("win-1"));
        QCoreApplication::processEvents();

        QVERIFY(!state->isFloating(QStringLiteral("win-3")));
    }

    void testOverflow_userFloatRemovesOverflowTracking()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-2")));

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 1000), QRect(500, 0, 500, 1000)});

        engine.unfloatWindow(QStringLiteral("win-2"));

        QVERIFY(!state->isFloating(QStringLiteral("win-2")));
        QCOMPARE(state->tiledWindowCount(), 2);
    }

    void testOverflow_screenRemovalCleansOverflow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-2")));

        engine.setAutotileScreens({});

        QVERIFY(!engine.isEnabled());
        // The per-screen state must go WITH the screen. isEnabled() alone is a
        // property of setAutotileScreens({}) and holds whether or not win-2's
        // tracking was released, so on its own it does not test this case's
        // name. Re-adding the screen is the observable check: the windows are
        // gone from the engine, so nothing is left floating from the old
        // overflow bookkeeping.
        // stateForScreen, NOT tilingStateForScreen: the latter is the creating
        // accessor (it materialises a state through the forKey factory), so it
        // can never answer null and would make this assertion vacuous.
        QVERIFY(engine.stateForScreen(screenName) == nullptr);
        QVERIFY(!engine.isWindowTracked(QStringLiteral("win-2")));
    }

    void testOverflow_crossScreenMigration()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screen1, screen2};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screen1);
        engine.windowOpened(QStringLiteral("win-2"), screen1);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);

        engine.config()->maxWindows = 1;
        state1->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile(screen1);
        QVERIFY(state1->isFloating(QStringLiteral("win-2")));

        engine.windowFocused(QStringLiteral("win-2"), screen2);
        QCoreApplication::processEvents();

        QVERIFY(!state1->containsWindow(QStringLiteral("win-2")));
        // The window must ARRIVE, not merely leave. Asserting only the source
        // side passes for a regression that drops the window on screen1 and
        // adopts it nowhere — which is the actual failure worth catching here,
        // since a dropped window is unmanaged rather than migrated.
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);
        QVERIFY(state2 != nullptr);
        QVERIFY2(state2->containsWindow(QStringLiteral("win-2")),
                 "the destination screen must adopt the migrated window");
        // The float bit CARRIES ACROSS the migration (insertShouldFloat), so
        // the window arrives floating rather than being re-tiled on the
        // destination. Pinned deliberately: it is the behaviour the arrival
        // path is written for, and a change to it would otherwise be silent.
        QVERIFY(state2->isFloating(QStringLiteral("win-2")));
    }

    void testOverflow_backfillPriority()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QCOMPARE(state->tiledWindowCount(), 3);

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 500), QRect(500, 0, 500, 500)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-3")));

        engine.config()->maxWindows = 3;
        state->setCalculatedZones({QRect(0, 0, 333, 500), QRect(333, 0, 333, 500), QRect(666, 0, 334, 500)});
        engine.retile(screenName);

        QVERIFY(!state->isFloating(QStringLiteral("win-3")));
    }

    void testOverflow_multipleUnfloat()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        engine.windowOpened(QStringLiteral("win-4"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 500), QRect(500, 0, 500, 500)});
        engine.retile(screenName);
        QCOMPARE(state->tiledWindowCount(), 2);

        engine.config()->maxWindows = 4;
        state->setCalculatedZones(
            {QRect(0, 0, 250, 500), QRect(250, 0, 250, 500), QRect(500, 0, 250, 500), QRect(750, 0, 250, 500)});

        // Recovery rides the PASSIVE channel: it is engine-initiated (the cap
        // freed a slot), not a user toggle, and the active signal's daemon
        // handler unconditionally shows the navigation OSD — so the active
        // channel popped a "Tiled" OSD the user never asked for. The passive
        // handler still does the same WTS bookkeeping for an unfloat
        // (setWindowFloating(false) + clearModeSpecificFloatMarker), and
        // applyGeometryForFloat never ran for this direction anyway, so
        // nothing but the OSD changes. The symmetric direction — overflow
        // FLOATING — already used the batch channel.
        QSignalSpy passiveSpy(&engine, &AutotileEngine::windowFloatingStateSynced);
        QSignalSpy activeSpy(&engine, &AutotileEngine::windowFloatingChanged);
        engine.retile(screenName);

        int unfloatCount = 0;
        for (int i = 0; i < passiveSpy.count(); ++i) {
            if (!passiveSpy.at(i).at(1).toBool()) {
                ++unfloatCount;
            }
        }
        QCOMPARE(unfloatCount, 2);
        // Positive control: no active emission, or the OSD would fire again.
        QCOMPARE(activeSpy.count(), 0);
    }

    void testOverflow_userFloatClearsTracking()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-2")));

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 1000), QRect(500, 0, 500, 1000)});
        engine.unfloatWindow(QStringLiteral("win-2"));
        engine.floatWindow(QStringLiteral("win-2"));

        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-2")));
    }

    void testOverflow_reentrantRetile()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});

        engine.retile(screenName);
        engine.retile(screenName);

        QVERIFY(state->isFloating(QStringLiteral("win-2")));
    }

    void testOverflow_multiScreenRemoval()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screen1, screen2};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screen1);
        engine.windowOpened(QStringLiteral("win-2"), screen1);
        engine.windowOpened(QStringLiteral("win-3"), screen2);
        engine.windowOpened(QStringLiteral("win-4"), screen2);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);

        engine.config()->maxWindows = 1;
        state1->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        state2->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile();
        QVERIFY(state1->isFloating(QStringLiteral("win-2")));
        QVERIFY(state2->isFloating(QStringLiteral("win-4")));

        engine.setAutotileScreens({screen2});
        QVERIFY(engine.isEnabled());

        PhosphorTiles::TilingState* state2After = engine.tilingStateForScreen(screen2);
        QVERIFY(state2After->isFloating(QStringLiteral("win-4")));
    }

    void testOverflow_perScreenMaxWindows()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QCOMPARE(state->tiledWindowCount(), 3);

        QVariantMap overrides;
        overrides[QStringLiteral("MaxWindows")] = 2;
        engine.applyPerScreenConfig(screenName, overrides);

        state->setCalculatedZones({QRect(0, 0, 500, 500), QRect(500, 0, 500, 500)});
        engine.retile(screenName);

        QCOMPARE(state->tiledWindowCount(), 2);
        QVERIFY(state->isFloating(QStringLiteral("win-3")));
    }

    void testOverflow_toggleFloatClearsTracking()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        engine.config()->maxWindows = 10;

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        state->setFocusedWindow(QStringLiteral("win-2"));

        engine.config()->maxWindows = 1;
        state->setCalculatedZones({QRect(0, 0, 1000, 1000)});
        engine.retile(screenName);
        QVERIFY(state->isFloating(QStringLiteral("win-2")));

        engine.toggleWindowFloat(QStringLiteral("win-2"), screenName);

        engine.config()->maxWindows = 2;
        state->setCalculatedZones({QRect(0, 0, 500, 1000), QRect(500, 0, 500, 1000)});
        engine.retile(screenName);

        QVERIFY(!state->isFloating(QStringLiteral("win-2")));
        QCOMPARE(state->tiledWindowCount(), 2);
    }
};

QTEST_MAIN(TestAutotileEngineOverflow)
#include "test_autotile_engine_overflow.moc"

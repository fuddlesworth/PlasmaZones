// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

using namespace PlasmaZones;

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

        QSignalSpy floatSpy(&engine, &AutotileEngine::windowFloatingChanged);
        engine.retile(screenName);

        int unfloatCount = 0;
        for (int i = 0; i < floatSpy.count(); ++i) {
            if (!floatSpy.at(i).at(1).toBool()) {
                ++unfloatCount;
            }
        }
        QCOMPARE(unfloatCount, 2);
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

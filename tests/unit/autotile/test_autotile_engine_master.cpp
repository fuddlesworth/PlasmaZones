// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace PlasmaZones;

/**
 * @brief AutotileEngine tests for master ratio/count adjustments,
 *        window lifecycle details, monocle flags, zone-ordered window
 *        transitions, and initial window order.
 */
class TestAutotileEngineMaster : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // Master ratio adjustment tests
    // =========================================================================

    void testIncreaseMasterRatio_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.setAutotileScreens({screen1, screen2});

        // Use windowOpened to properly set up m_windowToStateKey mappings
        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen2, 0, 0);
        QCoreApplication::processEvents(); // flush deferred retile

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);

        const qreal initial1 = state1->splitRatio();
        const qreal initial2 = state2->splitRatio();

        // Use windowFocused to properly set m_activeScreen on the engine
        engine.windowFocused(QStringLiteral("win1"), screen1);

        engine.increaseMasterRatio(0.1);

        // Only the focused screen's ratio should change
        QVERIFY(qFuzzyCompare(state1->splitRatio(), initial1 + 0.1));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), initial2));
    }

    void testDecreaseMasterRatio_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.setAutotileScreens({screen1, screen2});

        // Use windowOpened to properly set up m_windowToStateKey mappings
        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen2, 0, 0);
        QCoreApplication::processEvents(); // flush deferred retile

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);

        const qreal initial1 = state1->splitRatio();
        const qreal initial2 = state2->splitRatio();

        // Use windowFocused to properly set m_activeScreen on the engine
        engine.windowFocused(QStringLiteral("win1"), screen1);

        engine.decreaseMasterRatio(0.1);

        // Only the focused screen's ratio should change
        QVERIFY(qFuzzyCompare(state1->splitRatio(), initial1 - 0.1));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), initial2));
    }

    // =========================================================================
    // Master count adjustment tests
    // =========================================================================

    void testIncreaseMasterCount_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.setAutotileScreens({screen1, screen2});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win3"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win4"), screen2, 0, 0);
        engine.windowOpened(QStringLiteral("win5"), screen2, 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);
        const int initial1 = state1->masterCount();
        const int initial2 = state2->masterCount();

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.increaseMasterCount();

        QCOMPARE(state1->masterCount(), initial1 + 1);
        QCOMPARE(state2->masterCount(), initial2);
    }

    void testDecreaseMasterCount_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.setAutotileScreens({screen1, screen2});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win3"), screen2, 0, 0);
        engine.windowOpened(QStringLiteral("win4"), screen2, 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);
        state1->setMasterCount(2);
        state2->setMasterCount(2);

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.decreaseMasterCount();

        QCOMPARE(state1->masterCount(), 1);
        QCOMPARE(state2->masterCount(), 2);
    }

    void testDecreaseMasterCount_doesNotGoBelowOne()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        engine.setAutotileScreens({screen1});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen1);
        QCOMPARE(state->masterCount(), 1);

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.decreaseMasterCount();

        QCOMPARE(state->masterCount(), 1);
    }

    // =========================================================================
    // Per-screen override interaction tests
    // =========================================================================

    void testIncreaseMasterRatio_withPerScreenOverride_updatesOverrideNotGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        engine.setAutotileScreens({screen1});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        QCoreApplication::processEvents();

        // Apply a per-screen SplitRatio override
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.5;
        engine.applyPerScreenConfig(screen1, overrides);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen1);
        state->setSplitRatio(0.5);
        const qreal globalBefore = engine.config()->splitRatio;

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.increaseMasterRatio(0.1);

        // The per-screen state should be updated
        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.6));
        // The global config should NOT be updated
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, globalBefore));
        // The stored per-screen override should reflect the new value
        QVariantMap updatedOverrides = engine.perScreenOverrides(screen1);
        QVERIFY(qFuzzyCompare(updatedOverrides.value(QStringLiteral("SplitRatio")).toDouble(), 0.6));
    }

    void testDecreaseMasterRatio_withPerScreenOverride_updatesOverrideNotGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        engine.setAutotileScreens({screen1});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        QCoreApplication::processEvents();

        // Apply a per-screen SplitRatio override
        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.5;
        engine.applyPerScreenConfig(screen1, overrides);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen1);
        state->setSplitRatio(0.5);
        const qreal globalBefore = engine.config()->splitRatio;

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.decreaseMasterRatio(0.1);

        // The per-screen state should be updated
        QVERIFY(qFuzzyCompare(state->splitRatio(), 0.4));
        // The global config should NOT be updated
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, globalBefore));
        // The stored per-screen override should reflect the new value
        QVariantMap updatedOverrides = engine.perScreenOverrides(screen1);
        QVERIFY(qFuzzyCompare(updatedOverrides.value(QStringLiteral("SplitRatio")).toDouble(), 0.4));
    }

    void testIncreaseMasterCount_withPerScreenOverride_updatesOverrideNotGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        engine.setAutotileScreens({screen1});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win3"), screen1, 0, 0);
        QCoreApplication::processEvents();

        QVariantMap overrides;
        overrides[QStringLiteral("MasterCount")] = 1;
        engine.applyPerScreenConfig(screen1, overrides);

        const int globalBefore = engine.config()->masterCount;

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.increaseMasterCount();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screen1);
        QCOMPARE(state->masterCount(), 2);
        // Global config unchanged
        QCOMPARE(engine.config()->masterCount, globalBefore);
        // Stored override updated
        QVariantMap updatedOverrides = engine.perScreenOverrides(screen1);
        QCOMPARE(updatedOverrides.value(QStringLiteral("MasterCount")).toInt(), 2);
    }

    // =========================================================================
    // Monocle maximize tests
    // =========================================================================

    void testMonocle_tileRequestIncludesMonocleFlag()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.setAlgorithm(QLatin1String("monocle"));

        // Monocle flag requires >= 2 windows (single window is just normal tiling)
        engine.windowOpened(QStringLiteral("win-mono-1"), screenName);
        engine.windowOpened(QStringLiteral("win-mono-2"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        const QRect fullArea(10, 42, 1900, 1038);
        state->setCalculatedZones({fullArea, fullArea});
        engine.retile(screenName);

        QVERIFY(tiledSpy.count() >= 1);
        const QString json = tiledSpy.last().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray arr = doc.array();
        QVERIFY(!arr.isEmpty());
        for (const QJsonValue& val : arr) {
            QVERIFY(val.toObject().value(QLatin1String("monocle")).toBool(false));
        }
    }

    void testMonocle_nonMonocleTileRequestOmitsFlag()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.windowOpened(QStringLiteral("win-ms-1"), screenName);
        engine.windowOpened(QStringLiteral("win-ms-2"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        state->setCalculatedZones({QRect(10, 10, 950, 1060), QRect(960, 10, 950, 1060)});
        engine.retile(screenName);

        QVERIFY(tiledSpy.count() >= 1);
        const QString json = tiledSpy.last().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            QVERIFY(!val.toObject().contains(QLatin1String("monocle")));
        }
    }

    // =========================================================================
    // PhosphorZones::Zone-ordered window transition tests
    // =========================================================================

    void testSetInitialWindowOrder_preSeededInsertion()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B"), QStringLiteral("win-C")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-C"), screenName);
        engine.windowOpened(QStringLiteral("win-A"), screenName);
        engine.windowOpened(QStringLiteral("win-B"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.size(), 3);
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-A"));
        QCOMPARE(tiledWindows.at(1), QStringLiteral("win-B"));
        QCOMPARE(tiledWindows.at(2), QStringLiteral("win-C"));
    }

    void testSetInitialWindowOrder_ignoresWhenStateHasWindows()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-existing"), screenName);
        QCoreApplication::processEvents();

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-existing"));
    }

    void testSetInitialWindowOrder_unknownWindowsFallThrough()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        engine.windowOpened(QStringLiteral("win-unknown"), screenName);
        engine.windowOpened(QStringLiteral("win-B"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.size(), 3);
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-A"));
        QCOMPARE(tiledWindows.at(1), QStringLiteral("win-B"));
        QCOMPARE(tiledWindows.at(2), QStringLiteral("win-unknown"));
    }

    void testTiledWindowOrder_returnsOrderedList()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-1"), screenName);
        engine.windowOpened(QStringLiteral("win-2"), screenName);
        engine.windowOpened(QStringLiteral("win-3"), screenName);
        QCoreApplication::processEvents();

        QStringList order = engine.tiledWindowOrder(screenName);
        QCOMPARE(order.size(), 3);
        QCOMPARE(order.at(0), QStringLiteral("win-1"));
        QCOMPARE(order.at(1), QStringLiteral("win-2"));
        QCOMPARE(order.at(2), QStringLiteral("win-3"));
    }

    void testTiledWindowOrder_emptyForUnknownScreen()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QStringList order = engine.tiledWindowOrder(QStringLiteral("nonexistent"));
        QVERIFY(order.isEmpty());
    }

    void testRemoveWindow_cleansPendingInitialOrders()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B"), QStringLiteral("win-C")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        QCoreApplication::processEvents();
        engine.windowClosed(QStringLiteral("win-B"));

        engine.windowOpened(QStringLiteral("win-C"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.size(), 2);
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-A"));
        QCOMPARE(tiledWindows.at(1), QStringLiteral("win-C"));
    }

    void testSetInitialWindowOrder_guardChecksAllWindows()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-floating"), screenName);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(screenName);
        QVERIFY(state);
        state->setFloating(QStringLiteral("win-floating"), true);
        QVERIFY(state->tiledWindows().isEmpty());
        QCOMPARE(state->windowCount(), 1);

        QStringList preSeeded = {QStringLiteral("win-A")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        QCoreApplication::processEvents();

        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.size(), 1);
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-A"));
    }
};

QTEST_MAIN(TestAutotileEngineMaster)
#include "test_autotile_engine_master.moc"

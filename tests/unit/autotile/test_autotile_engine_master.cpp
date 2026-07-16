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
using namespace PhosphorTileEngine;

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
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
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

        // Use windowOpened to properly set up m_states mappings
        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen2, 0, 0);
        QCoreApplication::processEvents(); // flush deferred retile

        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.tilingStateForScreen(screen2);

        const qreal initial1 = state1->splitRatio();
        const qreal initial2 = state2->splitRatio();
        const qreal globalBefore = engine.config()->splitRatio;

        // Use windowFocused to properly set m_activeScreen on the engine
        engine.windowFocused(QStringLiteral("win1"), screen1);

        engine.increaseMasterRatio(0.1);

        // Only the focused screen's ratio should change
        QVERIFY(qFuzzyCompare(state1->splitRatio(), initial1 + 0.1));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), initial2));
        // A per-desktop ratio tweak must NOT bleed into the global config (no
        // override here) — otherwise it broadcasts to sibling screens / new
        // states on the next propagate.
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, globalBefore));
    }

    void testDecreaseMasterRatio_updatesFocusedScreenOnly()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString screen1 = QStringLiteral("Screen1");
        const QString screen2 = QStringLiteral("Screen2");
        engine.setAutotileScreens({screen1, screen2});

        // Use windowOpened to properly set up m_states mappings
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
        const int globalBefore = engine.config()->masterCount;

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.increaseMasterCount();

        QCOMPARE(state1->masterCount(), initial1 + 1);
        QCOMPARE(state2->masterCount(), initial2);
        // Per-desktop master-count tweak must NOT bleed into the global config.
        QCOMPARE(engine.config()->masterCount, globalBefore);
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

    // Tuning a ratio records the state's key in the engine's user-tuned set;
    // pruning that desktop must drop the key with the state (no dangling key, no
    // crash) so a reused desktop number starts clean.
    void testPruneTunedDesktop_isCrashSafe()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");
        engine.setAutotileScreens({screen1});

        engine.windowOpened(QStringLiteral("win1"), screen1, 0, 0);
        engine.windowOpened(QStringLiteral("win2"), screen1, 0, 0);
        QCoreApplication::processEvents();

        engine.windowFocused(QStringLiteral("win1"), screen1);
        engine.increaseMasterRatio(0.1); // tunes the current desktop's state

        engine.pruneStatesForDesktop(engine.currentDesktop());

        // A fresh state on the same screen is created cleanly after the prune.
        engine.windowOpened(QStringLiteral("win3"), screen1, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(engine.tilingStateForScreen(screen1) != nullptr);
    }

    // =========================================================================
    // Global setters vs per-desktop tunings
    //
    // setGlobalSplitRatio/setGlobalMasterCount drop the user-tuned flag for
    // EVERY key, on every desktop. The write has to reach just as far. If it
    // stops at the current desktop, a tuned state on another desktop keeps its
    // tuned VALUE while losing the flag that protected it, and the next
    // propagateGlobal* to run while that desktop is current silently overwrites
    // the user's value — a clobber deferred until some unrelated settings
    // refresh, long after the global set that caused it.
    // =========================================================================

    void testSetGlobalSplitRatio_reachesTunedStateOnAnotherDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");

        // Desktop 2: tune the ratio locally, the way Meta+= does.
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d2"), screen1, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(screen1);
        engine.windowFocused(QStringLiteral("win-d2"), screen1);
        d2->setSplitRatio(0.6);
        engine.increaseMasterRatio(0.1); // → 0.7, and marks desktop 2 user-tuned
        QVERIFY(qFuzzyCompare(d2->splitRatio(), 0.7));

        // Desktop 1: an absolute global set, e.g. the D-Bus masterRatio property.
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d1"), screen1, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(screen1);
        QVERIFY(d1 != d2);

        engine.setGlobalSplitRatio(0.35);

        // The current desktop takes the new value, and so does desktop 2: the set
        // dropped desktop 2's tuned flag, so it must also write desktop 2's value.
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.35));
        QVERIFY(qFuzzyCompare(d1->splitRatio(), 0.35));
        QVERIFY2(qFuzzyCompare(d2->splitRatio(), 0.35),
                 "a global set that drops every tuned flag must write every state it unprotected");
    }

    void testSetGlobalMasterCount_reachesTunedStateOnAnotherDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");

        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d2"), screen1, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(screen1);
        engine.windowFocused(QStringLiteral("win-d2"), screen1);
        d2->setMasterCount(1);
        engine.increaseMasterCount(); // → 2, and marks desktop 2 user-tuned
        QCOMPARE(d2->masterCount(), 2);

        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d1"), screen1, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d1 = engine.tilingStateForScreen(screen1);
        QVERIFY(d1 != d2);

        engine.setGlobalMasterCount(3);

        QCOMPARE(engine.config()->masterCount, 3);
        QCOMPARE(d1->masterCount(), 3);
        QCOMPARE(d2->masterCount(), 3);
    }

    // The delayed half of the same bug, driven end to end: with the flag dropped
    // but the value left behind, this propagate is what actually destroys the
    // user's ratio — triggered by an unrelated settings refresh, back on the
    // desktop they tuned.
    void testSetGlobalSplitRatio_thenPropagateOnTunedDesktop_holdsTheSetValue()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("Screen1");

        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d2"), screen1, 0, 0);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* d2 = engine.tilingStateForScreen(screen1);
        engine.windowFocused(QStringLiteral("win-d2"), screen1);
        d2->setSplitRatio(0.6);
        engine.increaseMasterRatio(0.1);

        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({screen1});
        engine.windowOpened(QStringLiteral("win-d1"), screen1, 0, 0);
        QCoreApplication::processEvents();

        engine.setGlobalSplitRatio(0.35);

        // Back on desktop 2, a settings refresh runs propagateGlobalSplitRatio.
        // Desktop 2 already holds 0.35, so the propagate is a no-op and the value
        // the user last saw set is the value that survives.
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({screen1});
        engine.refreshConfigFromSettings();

        QVERIFY(qFuzzyCompare(d2->splitRatio(), 0.35));
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

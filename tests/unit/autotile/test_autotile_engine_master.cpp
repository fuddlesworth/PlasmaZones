// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/TilingState.h"
#include "autotile/AlgorithmRegistry.h"
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

    void testIncreaseMasterRatio_updatesAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        TilingState* state1 = engine.stateForScreen(QStringLiteral("Screen1"));
        TilingState* state2 = engine.stateForScreen(QStringLiteral("Screen2"));

        const qreal initial1 = state1->splitRatio();
        const qreal initial2 = state2->splitRatio();

        engine.increaseMasterRatio(0.1);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), initial1 + 0.1));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), initial2 + 0.1));
    }

    void testDecreaseMasterRatio_updatesAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        TilingState* state1 = engine.stateForScreen(QStringLiteral("Screen1"));
        TilingState* state2 = engine.stateForScreen(QStringLiteral("Screen2"));
        const qreal initial1 = state1->splitRatio();
        const qreal initial2 = state2->splitRatio();

        engine.decreaseMasterRatio(0.1);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), initial1 - 0.1));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), initial2 - 0.1));
    }

    // =========================================================================
    // Master count adjustment tests
    // =========================================================================

    void testIncreaseMasterCount_updatesAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        TilingState* state = engine.stateForScreen(QStringLiteral("Screen1"));

        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->addWindow(QStringLiteral("win3"));

        const int initial = state->masterCount();

        engine.increaseMasterCount();

        QCOMPARE(state->masterCount(), initial + 1);
    }

    void testDecreaseMasterCount_updatesAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        TilingState* state = engine.stateForScreen(QStringLiteral("Screen1"));
        state->addWindow(QStringLiteral("win1"));
        state->addWindow(QStringLiteral("win2"));
        state->setMasterCount(2);

        engine.decreaseMasterCount();

        QCOMPARE(state->masterCount(), 1);
    }

    void testDecreaseMasterCount_doesNotGoBelowOne()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        TilingState* state = engine.stateForScreen(QStringLiteral("Screen1"));
        state->addWindow(QStringLiteral("win1"));
        QCOMPARE(state->masterCount(), 1);

        engine.decreaseMasterCount();

        QCOMPARE(state->masterCount(), 1);
    }

    // =========================================================================
    // Monocle maximize tests
    // =========================================================================

    void testMonocle_tileRequestIncludesMonocleFlag()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.setAlgorithm(QLatin1String("monocle"));

        // Monocle flag requires >= 2 windows (single window is just normal tiling)
        engine.windowOpened(QStringLiteral("win-mono-1"), screenName);
        engine.windowOpened(QStringLiteral("win-mono-2"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        TilingState* state = engine.stateForScreen(screenName);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.windowOpened(QStringLiteral("win-ms-1"), screenName);
        engine.windowOpened(QStringLiteral("win-ms-2"), screenName);
        QCoreApplication::processEvents();

        QSignalSpy tiledSpy(&engine, &AutotileEngine::windowsTiled);

        TilingState* state = engine.stateForScreen(screenName);
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
    // Zone-ordered window transition tests
    // =========================================================================

    void testSetInitialWindowOrder_preSeededInsertion()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B"), QStringLiteral("win-C")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-C"), screenName);
        engine.windowOpened(QStringLiteral("win-A"), screenName);
        engine.windowOpened(QStringLiteral("win-B"), screenName);
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screenName);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-existing"), screenName);
        QCoreApplication::processEvents();

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-existing"));
    }

    void testSetInitialWindowOrder_unknownWindowsFallThrough()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        engine.windowOpened(QStringLiteral("win-unknown"), screenName);
        engine.windowOpened(QStringLiteral("win-B"), screenName);
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screenName);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
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
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QStringList order = engine.tiledWindowOrder(QStringLiteral("nonexistent"));
        QVERIFY(order.isEmpty());
    }

    void testRemoveWindow_cleansPendingInitialOrders()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        QStringList preSeeded = {QStringLiteral("win-A"), QStringLiteral("win-B"), QStringLiteral("win-C")};
        engine.setInitialWindowOrder(screenName, preSeeded);

        engine.windowOpened(QStringLiteral("win-A"), screenName);
        QCoreApplication::processEvents();
        engine.windowClosed(QStringLiteral("win-B"));

        engine.windowOpened(QStringLiteral("win-C"), screenName);
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screenName);
        QVERIFY(state);
        QStringList tiledWindows = state->tiledWindows();
        QCOMPARE(tiledWindows.size(), 2);
        QCOMPARE(tiledWindows.at(0), QStringLiteral("win-A"));
        QCOMPARE(tiledWindows.at(1), QStringLiteral("win-C"));
    }

    void testSetInitialWindowOrder_guardChecksAllWindows()
    {
        const QString screenName = QStringLiteral("DP-1");
        AutotileEngine engine(nullptr, nullptr, nullptr);
        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowOpened(QStringLiteral("win-floating"), screenName);
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screenName);
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "core/constants.h"

using namespace PlasmaZones;

/**
 * @brief Extended tests for algorithm switch behaviors
 */
class TestAutotileExtAlgoSwitch : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        AlgorithmRegistry::instance();
    }

    void testAlgorithmSwitch_splitRatioResetToNewDefault()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::MasterStack);
        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::BSP);
        QVERIFY(msAlgo && bspAlgo);

        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, msAlgo->defaultSplitRatio()));

        engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);

        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, bspAlgo->defaultSplitRatio()));
    }

    void testAlgorithmSwitch_maxWindowsResetWhenUnchanged()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::MasterStack);
        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::BSP);
        QVERIFY(msAlgo && bspAlgo);

        engine.config()->maxWindows = msAlgo->defaultMaxWindows();

        engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);

        QCOMPARE(engine.config()->maxWindows, bspAlgo->defaultMaxWindows());
    }

    void testAlgorithmSwitch_maxWindowsPreservedWhenCustomized()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::MasterStack);
        QVERIFY(msAlgo);

        const int customMax = msAlgo->defaultMaxWindows() + 3;
        engine.config()->maxWindows = customMax;

        engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);

        QCOMPARE(engine.config()->maxWindows, customMax);
    }

    void testAlgorithmSwitch_centeredMasterPreservesPerAlgoValues()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);

        engine.config()->centeredMasterSplitRatio = 0.45;
        engine.config()->centeredMasterMasterCount = 2;

        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        // Switch TO centered-master -- should use per-algorithm values
        const QString centeredMasterId = QStringLiteral("centered-master");
        if (!AlgorithmRegistry::instance()->hasAlgorithm(centeredMasterId)) {
            QSKIP("centered-master algorithm not registered in this build");
        }

        engine.setAlgorithm(centeredMasterId);
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.45));
        QCOMPARE(engine.config()->masterCount, 2);
    }

    void testAlgorithmSwitch_propagatesRatioToAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        TilingState* state1 = engine.stateForScreen(QStringLiteral("eDP-1"));
        TilingState* state2 = engine.stateForScreen(QStringLiteral("HDMI-1"));
        state1->addWindow(QStringLiteral("win1"));
        state2->addWindow(QStringLiteral("win2"));

        engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);

        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::BSP);
        QVERIFY(bspAlgo);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), bspAlgo->defaultSplitRatio()));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), bspAlgo->defaultSplitRatio()));
    }

    void testAlgorithmSwitch_skipsScreensWithPerScreenOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        engine.applyPerScreenConfig(screen2, overrides);

        TilingState* state1 = engine.stateForScreen(screen1);
        TilingState* state2 = engine.stateForScreen(screen2);

        QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.8));

        engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);

        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::BSP);
        QVERIFY(bspAlgo);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), bspAlgo->defaultSplitRatio()));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.8));
    }

    void testAlgorithmSwitch_backfillWhenMaxWindowsIncreases()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr);
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Start from MasterStack (which has fewer default maxWindows than BSP)
        auto* msAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::MasterStack);
        QVERIFY(msAlgo);
        engine.setAlgorithm(DBus::AutotileAlgorithm::MasterStack);

        engine.config()->maxWindows = msAlgo->defaultMaxWindows();

        for (int i = 1; i <= msAlgo->defaultMaxWindows() + 2; ++i) {
            engine.windowOpened(QStringLiteral("win%1").arg(i), screen);
        }
        QCoreApplication::processEvents();

        TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);

        auto* bspAlgo = AlgorithmRegistry::instance()->algorithm(DBus::AutotileAlgorithm::BSP);
        if (bspAlgo && bspAlgo->defaultMaxWindows() > msAlgo->defaultMaxWindows()) {
            engine.setAlgorithm(DBus::AutotileAlgorithm::BSP);
            QCoreApplication::processEvents();

            QCOMPARE(engine.config()->maxWindows, bspAlgo->defaultMaxWindows());
        }
    }
};

QTEST_MAIN(TestAutotileExtAlgoSwitch)
#include "test_autotile_ext_algoswitch.moc"

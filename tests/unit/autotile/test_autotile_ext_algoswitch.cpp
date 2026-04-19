// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "autotile/AutotileEngine.h"
#include "../helpers/AutotileTestHelpers.h"
#include "autotile/AutotileConfig.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;

/**
 * @brief Extended tests for algorithm switch behaviors
 */
class TestAutotileExtAlgoSwitch : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    void testAlgorithmSwitch_splitRatioResetToNewDefault()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAlgorithm(QLatin1String("master-stack"));

        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        QVERIFY(msAlgo && bspAlgo);

        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, msAlgo->defaultSplitRatio()));

        engine.setAlgorithm(QLatin1String("bsp"));

        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, bspAlgo->defaultSplitRatio()));
    }

    void testAlgorithmSwitch_maxWindowsResetWhenUnchanged()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAlgorithm(QLatin1String("master-stack"));

        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        QVERIFY(msAlgo && bspAlgo);

        engine.config()->maxWindows = msAlgo->defaultMaxWindows();

        engine.setAlgorithm(QLatin1String("bsp"));

        QCOMPARE(engine.config()->maxWindows, bspAlgo->defaultMaxWindows());
    }

    void testAlgorithmSwitch_maxWindowsPreservedWhenCustomized()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAlgorithm(QLatin1String("master-stack"));

        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        QVERIFY(msAlgo);

        const int customMax = msAlgo->defaultMaxWindows() + 3;
        engine.config()->maxWindows = customMax;

        engine.setAlgorithm(QLatin1String("bsp"));

        QCOMPARE(engine.config()->maxWindows, customMax);
    }

    void testAlgorithmSwitch_centeredMasterPreservesPerAlgoValues()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        // Pre-populate saved settings for centered-master
        AlgorithmSettings cmSaved;
        cmSaved.splitRatio = 0.45;
        cmSaved.masterCount = 2;
        engine.config()->savedAlgorithmSettings[QStringLiteral("centered-master")] = cmSaved;

        engine.setAlgorithm(QLatin1String("master-stack"));

        // Switch TO centered-master -- should use per-algorithm values
        const QString centeredMasterId = QStringLiteral("centered-master");
        if (!m_scriptSetup.registry()->hasAlgorithm(centeredMasterId)) {
            QSKIP("centered-master algorithm not registered in this build");
        }

        engine.setAlgorithm(centeredMasterId);
        QVERIFY(qFuzzyCompare(engine.config()->splitRatio, 0.45));
        QCOMPARE(engine.config()->masterCount, 2);
    }

    void testAlgorithmSwitch_propagatesRatioToAllScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        engine.setAlgorithm(QLatin1String("master-stack"));

        PhosphorTiles::TilingState* state1 = engine.stateForScreen(QStringLiteral("eDP-1"));
        PhosphorTiles::TilingState* state2 = engine.stateForScreen(QStringLiteral("HDMI-1"));
        state1->addWindow(QStringLiteral("win1"));
        state2->addWindow(QStringLiteral("win2"));

        engine.setAlgorithm(QLatin1String("bsp"));

        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        QVERIFY(bspAlgo);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), bspAlgo->defaultSplitRatio()));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), bspAlgo->defaultSplitRatio()));
    }

    void testAlgorithmSwitch_skipsScreensWithPerScreenOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen1 = QStringLiteral("eDP-1");
        const QString screen2 = QStringLiteral("HDMI-1");
        engine.setAutotileScreens({screen1, screen2});
        engine.setAlgorithm(QLatin1String("master-stack"));

        QVariantMap overrides;
        overrides[QStringLiteral("SplitRatio")] = 0.8;
        engine.applyPerScreenConfig(screen2, overrides);

        PhosphorTiles::TilingState* state1 = engine.stateForScreen(screen1);
        PhosphorTiles::TilingState* state2 = engine.stateForScreen(screen2);

        QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.8));

        engine.setAlgorithm(QLatin1String("bsp"));

        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        QVERIFY(bspAlgo);

        QVERIFY(qFuzzyCompare(state1->splitRatio(), bspAlgo->defaultSplitRatio()));
        QVERIFY(qFuzzyCompare(state2->splitRatio(), 0.8));
    }

    void testAlgorithmSwitch_backfillWhenMaxWindowsIncreases()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screen = QStringLiteral("eDP-1");
        engine.setAutotileScreens({screen});

        // Start from MasterStack (which has fewer default maxWindows than BSP)
        auto* msAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("master-stack"));
        QVERIFY(msAlgo);
        engine.setAlgorithm(QLatin1String("master-stack"));

        engine.config()->maxWindows = msAlgo->defaultMaxWindows();

        for (int i = 1; i <= msAlgo->defaultMaxWindows() + 2; ++i) {
            engine.windowOpened(QStringLiteral("win%1").arg(i), screen);
        }
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.stateForScreen(screen);
        QVERIFY(state);

        auto* bspAlgo = m_scriptSetup.registry()->algorithm(QLatin1String("bsp"));
        if (bspAlgo && bspAlgo->defaultMaxWindows() > msAlgo->defaultMaxWindows()) {
            engine.setAlgorithm(QLatin1String("bsp"));
            QCoreApplication::processEvents();

            QCOMPARE(engine.config()->maxWindows, bspAlgo->defaultMaxWindows());
        }
    }
};

QTEST_MAIN(TestAutotileExtAlgoSwitch)
#include "test_autotile_ext_algoswitch.moc"

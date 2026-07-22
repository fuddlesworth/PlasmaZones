// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_algorithm_controller.cpp
 * @brief Tests for TilingAlgorithmController's per-algorithm persistence.
 *
 * The tiling page writes per-algorithm split ratio / master count / max windows
 * into the PerAlgorithmSettings config map. A slot that merely echoes the
 * algorithm's own default carries no user intent, so persisting it would surface
 * as a spurious "you changed this" row in the config profile diff. These tests pin
 * that writing a field equal to the algorithm's default does NOT create (or leaves
 * pruned) a slot, while a genuine customization on another field survives.
 */

#include <QTest>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>

#include "settings/pages/tilingalgorithmcontroller.h"
#include "../helpers/ScriptedAlgoTestSetup.h"
#include "../helpers/StubSettings.h"

using namespace PlasmaZones;

class TestTilingAlgorithmController : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

    static QVariantMap gridEntry(const StubSettings& settings)
    {
        return settings.autotilePerAlgorithmSettings().value(QStringLiteral("grid")).toMap();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
    }

    // Setting a field to the algorithm's own default must not author a slot: grid
    // at its default max windows is not a customization, and a persisted
    // {grid:{maxWindows:9}} would read as a change the user never made.
    void setDefaultMaxWindows_writesNoSlot()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* grid = m_scriptSetup.registry()->algorithm(QStringLiteral("grid"));
        QVERIFY(grid);

        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), grid->defaultMaxWindows());

        QVERIFY2(!settings.autotilePerAlgorithmSettings().contains(QStringLiteral("grid")),
                 "a default-valued max-windows write leaked a per-algorithm slot");
    }

    // Customizing then resetting to the default must leave nothing behind, matching
    // the state as if the field had never been touched.
    void customThenResetToDefault_prunesSlot()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* grid = m_scriptSetup.registry()->algorithm(QStringLiteral("grid"));
        QVERIFY(grid);
        const int def = grid->defaultMaxWindows();
        const int custom = def - 2; // grid default is 9; 7 is a safe in-range non-default
        QVERIFY(custom != def);

        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), custom);
        QCOMPARE(gridEntry(settings).value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toInt(), custom);

        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), def);
        QVERIFY2(!settings.autotilePerAlgorithmSettings().contains(QStringLiteral("grid")),
                 "resetting the sole customized field to default did not prune the slot");
    }

    // Resetting one field to default must drop only that field, preserving an
    // unrelated customization in the same slot.
    void resetOneField_keepsOtherCustomization()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* grid = m_scriptSetup.registry()->algorithm(QStringLiteral("grid"));
        QVERIFY(grid);
        const int mwDefault = grid->defaultMaxWindows();
        const qreal srCustom = 0.7; // grid's default split ratio is 0.5
        QVERIFY(!qFuzzyCompare(1.0 + srCustom, 1.0 + grid->defaultSplitRatio()));

        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), mwDefault - 2);
        controller.setAlgorithmSplitRatio(QStringLiteral("grid"), srCustom);
        QVERIFY(gridEntry(settings).contains(PhosphorTiles::AutotileJsonKeys::MaxWindows));
        QVERIFY(gridEntry(settings).contains(PhosphorTiles::AutotileJsonKeys::SplitRatio));

        // Reset only max windows; the split-ratio customization must survive.
        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), mwDefault);
        QVERIFY2(settings.autotilePerAlgorithmSettings().contains(QStringLiteral("grid")),
                 "slot with a remaining customization was pruned");
        QVERIFY2(!gridEntry(settings).contains(PhosphorTiles::AutotileJsonKeys::MaxWindows),
                 "reset max-windows field was not dropped");
        QCOMPARE(gridEntry(settings).value(PhosphorTiles::AutotileJsonKeys::SplitRatio).toDouble(), srCustom);
    }
};

QTEST_MAIN(TestTilingAlgorithmController)
#include "test_tiling_algorithm_controller.moc"

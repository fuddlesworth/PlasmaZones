// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_algorithm_controller.cpp
 * @brief Tests for TilingAlgorithmController's per-algorithm persistence.
 *
 * The tiling page writes per-algorithm split ratio / master count / max windows
 * into the PerAlgorithmSettings config map. Two invariants are pinned here:
 *
 * 1. A slot whose every field echoes the algorithm's own defaults carries no
 *    user intent and is never authored (and is pruned when a reset makes it
 *    so) — persisting it would surface as a spurious "you changed this" row
 *    in the config profile diff.
 * 2. A slot that IS authored is fully materialized with the algorithm's own
 *    defaults for the untouched fields. The Settings sanitize pass refills
 *    missing fields with the GENERIC schema defaults, which are wrong for any
 *    algorithm whose own defaults differ (grid's max windows is 9, the
 *    generic default 5). Thin slots therefore cannot round-trip: the
 *    BullHorn report (July 2026) hit exactly that as a silent no-op when
 *    setting centered-master's max windows to its own default of 7.
 */

#include <QTest>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/TilingAlgorithm.h>

#include "settings/pages/tilingalgorithmcontroller.h"
#include "helpers/ScriptedAlgoTestSetup.h"
#include "helpers/StubSettings.h"

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

    // Resetting one field to default must keep the slot alive for the
    // remaining customization, with the reset field stored explicitly at the
    // algorithm's default — a dropped field would be refilled with the
    // GENERIC default by the Settings sanitize pass and read back wrong.
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
        QCOMPARE(gridEntry(settings).value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toInt(), mwDefault);
        QCOMPARE(gridEntry(settings).value(PhosphorTiles::AutotileJsonKeys::SplitRatio).toDouble(), srCustom);
    }

    // An authored slot must be fully materialized with the ALGORITHM'S own
    // defaults for the untouched fields. The Settings sanitize pass
    // (perAlgoFromVariantMap) fills missing fields with the generic schema
    // defaults, so a thin {splitRatio} slot for grid would read back with
    // maxWindows 5 instead of grid's own 9 and silently re-cap the algorithm.
    void customizeOneField_materializesAlgorithmDefaults()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* grid = m_scriptSetup.registry()->algorithm(QStringLiteral("grid"));
        QVERIFY(grid);
        QVERIFY(grid->defaultMaxWindows() != PhosphorTiles::AutotileDefaults::DefaultMaxWindows);

        controller.setAlgorithmSplitRatio(QStringLiteral("grid"), 0.7);

        const QVariantMap entry = gridEntry(settings);
        QVERIFY2(entry.contains(PhosphorTiles::AutotileJsonKeys::MaxWindows),
                 "authored slot left max windows thin — the sanitizer would refill it with the generic default");
        QCOMPARE(entry.value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toInt(), grid->defaultMaxWindows());
        QVERIFY(entry.contains(PhosphorTiles::AutotileJsonKeys::MasterCount));
        QCOMPARE(entry.value(PhosphorTiles::AutotileJsonKeys::MasterCount).toInt(),
                 PhosphorTiles::AutotileDefaults::DefaultMasterCount);
    }

    // setCustomParam is the other slot author and must uphold the same
    // materialization contract: a custom-param write on an algorithm with no
    // prior slot must not leave the numeric fields thin, or the sanitizer
    // refills the max windows with the generic 5 and silently re-caps the
    // algorithm the moment any custom param is touched. Cluster declares
    // custom params AND a non-generic defaultMaxWindows (8), which is the
    // combination that exposed the bug.
    void setCustomParam_materializesAlgorithmDefaults()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* cluster = m_scriptSetup.registry()->algorithm(QStringLiteral("cluster"));
        QVERIFY(cluster);
        QVERIFY(cluster->supportsCustomParams());
        QVERIFY(cluster->defaultMaxWindows() != PhosphorTiles::AutotileDefaults::DefaultMaxWindows);

        const QVariantList defs = cluster->customParamDefList();
        QVERIFY(!defs.isEmpty());
        const QVariantMap def = defs.first().toMap();
        const QString paramName = def.value(QStringLiteral("name")).toString();
        QVERIFY(!paramName.isEmpty());

        controller.setCustomParam(QStringLiteral("cluster"), paramName, def.value(QStringLiteral("defaultValue")));

        const QVariantMap entry = settings.autotilePerAlgorithmSettings().value(QStringLiteral("cluster")).toMap();
        QVERIFY2(entry.contains(PhosphorTiles::AutotileJsonKeys::MaxWindows),
                 "custom-param write left max windows thin — the sanitizer would refill it with the generic default");
        QCOMPARE(entry.value(PhosphorTiles::AutotileJsonKeys::MaxWindows).toInt(), cluster->defaultMaxWindows());
        QVERIFY(entry.contains(PhosphorTiles::AutotileJsonKeys::SplitRatio));
        QVERIFY(entry.contains(PhosphorTiles::AutotileJsonKeys::MasterCount));
    }

    // Regression for the BullHorn report: a stale sanitizer-shaped slot pins
    // the generic max windows (5), and the user sets the value back to the
    // algorithm's own default. Under the old delete-the-field contract this
    // collapsed into a silent no-op (field removed, sanitizer refilled it
    // with 5, equality bail) and the daemon kept evicting at 5. The write
    // must land: with every field back at the algorithm's defaults the slot
    // is pruned, and every reader derives the algorithm's own default (9).
    void defaultValueWrite_replacesStaleGenericSlot()
    {
        StubSettings settings;
        TilingAlgorithmController controller(settings, *m_scriptSetup.registry());

        auto* grid = m_scriptSetup.registry()->algorithm(QStringLiteral("grid"));
        QVERIFY(grid);
        QVERIFY(grid->defaultMaxWindows() != PhosphorTiles::AutotileDefaults::DefaultMaxWindows);

        // Sanitizer-shaped stale slot: generic defaults materialized into every field.
        QVariantMap staleEntry;
        staleEntry[PhosphorTiles::AutotileJsonKeys::SplitRatio] = grid->defaultSplitRatio();
        staleEntry[PhosphorTiles::AutotileJsonKeys::MasterCount] = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
        staleEntry[PhosphorTiles::AutotileJsonKeys::MaxWindows] = PhosphorTiles::AutotileDefaults::DefaultMaxWindows;
        QVariantMap perAlgo;
        perAlgo[QStringLiteral("grid")] = staleEntry;
        settings.setAutotilePerAlgorithmSettings(perAlgo);

        controller.setAlgorithmMaxWindows(QStringLiteral("grid"), grid->defaultMaxWindows());

        QVERIFY2(!settings.autotilePerAlgorithmSettings().contains(QStringLiteral("grid")),
                 "stale generic-default slot survived a write of the algorithm's own default — "
                 "the wrong cap would keep pinning the algorithm");
    }
};

QTEST_MAIN(TestTilingAlgorithmController)
#include "test_tiling_algorithm_controller.moc"

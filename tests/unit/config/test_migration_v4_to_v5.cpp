// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v4_to_v5.cpp
 * @brief Unit tests for the v4 → v5 schema migration (config → config).
 *
 * The v4 schema stored per-mode (separate Snapping vs Tiling) window appearance
 * (borders, title bars, colours) and gap settings in config.json, plus per-screen
 * gap subsets under PerScreen/{Snapping,Autotile}/<screen>. v5 UNIFIES these:
 *   - the two per-mode appearance value sets collapse into ONE top-level
 *     "Windows" config group,
 *   - the two per-mode global gap value sets collapse into ONE top-level "Gaps"
 *     config group,
 *   - the per-screen gap subsets collapse per monitor into the per-screen
 *     autotile gap keys (PerScreen/Autotile/<screen>), and the gap-only
 *     PerScreen/Snapping subtree is dropped.
 * The migration creates NO rules and writes only values that DIFFER from the v4
 * compile defaults. Collapse rule per field: prefer the value differing from the
 * default; on a tie prefer SNAPPING.
 *
 * A v4 config.json fixture (plus a pre-existing empty v4 rules.json, mirroring a
 * real upgrade) is run through ConfigMigration::ensureJsonConfig; the tests
 * assert the resulting config groups, that no rules are created, and that the
 * consumed v4 groups are removed while survivors (AdjacentThreshold / SmartGaps)
 * are preserved.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configkeys.h"
#include "../../../src/config/configmigration.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/RuleSet.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV4ToV5 : public QObject
{
    Q_OBJECT

private:
    void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(obj).toJson());
    }

    QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    /// Seed a valid, empty v4 rules.json so the migration chain's rule-store
    /// steps take their cleanup-only branch, mirroring a real upgrade where the
    /// store was written during the user's earlier v3→v4 migration.
    void seedEmptyRules()
    {
        PhosphorRules::RuleSet set;
        QDir().mkpath(QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath());
        QVERIFY(set.saveToFile(ConfigDefaults::rulesFilePath()));
    }

    QJsonArray rules()
    {
        return readJson(ConfigDefaults::rulesFilePath()).value(QStringLiteral("rules")).toArray();
    }

    /// Merge @p keys into the nested group at @p segments of @p root, preserving
    /// existing siblings.
    void setNested(QJsonObject& root, const QStringList& segments, const QJsonObject& keys)
    {
        QList<QJsonObject> chain;
        QJsonObject node = root;
        for (int i = 0; i < segments.size(); ++i) {
            chain.append(node);
            node = node.value(segments.at(i)).toObject();
        }
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            node.insert(it.key(), it.value());
        }
        QJsonObject child = node;
        for (int i = segments.size() - 1; i >= 0; --i) {
            QJsonObject parent = chain.at(i);
            parent.insert(segments.at(i), child);
            child = parent;
        }
        root = child;
    }

    QJsonObject baseV4Config()
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 4);
        return root;
    }

    /// The migrated "Windows" appearance group.
    QJsonObject windowsGroup()
    {
        return readJson(ConfigDefaults::configFilePath()).value(ConfigKeys::windowsAppearanceGroup()).toObject();
    }

    /// The migrated "Gaps" group.
    QJsonObject gapsGroup()
    {
        return readJson(ConfigDefaults::configFilePath()).value(ConfigKeys::gapsGroup()).toObject();
    }

    /// The per-screen autotile group for @p screen (PerScreen/Autotile/<screen>).
    QJsonObject perScreenAutotile(const QString& screen)
    {
        return readJson(ConfigDefaults::configFilePath())
            .value(QStringLiteral("PerScreen"))
            .toObject()
            .value(QStringLiteral("Autotile"))
            .toObject()
            .value(screen)
            .toObject();
    }

private Q_SLOTS:

    // ─── Clean default v4 → no config overrides, no rules ──────────────────

    void testCleanDefaultV4_producesNoOverrides()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        // Every appearance/gap value at its v4 default — nothing to migrate.
        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("ShowBorder"), false}, {QStringLiteral("Width"), 2}, {QStringLiteral("Radius"), 8}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Inner"), 8}, {QStringLiteral("Outer"), 8}});
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Inner"), 8}, {QStringLiteral("Outer"), 8}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // No rules created; all-default values write no Windows/Gaps keys.
        QCOMPARE(rules().size(), 0);
        QVERIFY2(windowsGroup().isEmpty(), "no differing appearance value → no Windows group");
        QVERIFY2(gapsGroup().isEmpty(), "no differing gap value → no Gaps group");

        // Config stamped at the current schema version.
        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }

    // ─── Customised border width + inner gap → unified config groups ───────

    void testCustomBorderWidthAndInnerGap_collapsedToConfig()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Snapping border width differs (5 != default 2).
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        // Tiling inner gap differs (12 != default 8).
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")}, {{QStringLiteral("Inner"), 12}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(rules().size(), 0);
        QCOMPARE(windowsGroup().value(ConfigKeys::widthKey()).toInt(), 5);
        QCOMPARE(gapsGroup().value(ConfigKeys::innerGapKey()).toInt(), 12);
    }

    // On a conflicting field, the SNAPPING value wins the collapse.
    void testCollapse_snappingWinsOverTiling()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Both modes set a differing (non-default) border width; snapping wins.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 7}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(windowsGroup().value(ConfigKeys::widthKey()).toInt(), 5);
    }

    // ─── Colours ──────────────────────────────────────────────────────────

    void testColors_useSystemFalse_carriesHex()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Colors")},
                  {{QStringLiteral("UseSystem"), false},
                   {QStringLiteral("Active"), QStringLiteral("#ffff0000")},
                   {QStringLiteral("Inactive"), QStringLiteral("#ff00ff00")}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject windows = windowsGroup();
        QCOMPARE(windows.value(ConfigKeys::borderColorActiveKey()).toString(), QStringLiteral("#ffff0000"));
        QCOMPARE(windows.value(ConfigKeys::borderColorInactiveKey()).toString(), QStringLiteral("#ff00ff00"));
    }

    void testColors_useSystemTrue_noColorKey()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // useSystem=true is the v4 default (accent), so the colours contribute
        // nothing; a differing border width keeps a Windows group so we assert the
        // ABSENCE of any colour key rather than the absence of the group.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Colors")},
                  {{QStringLiteral("UseSystem"), true},
                   {QStringLiteral("Active"), QStringLiteral("#ffff0000")},
                   {QStringLiteral("Inactive"), QStringLiteral("#ff00ff00")}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject windows = windowsGroup();
        QCOMPARE(windows.value(ConfigKeys::widthKey()).toInt(), 5);
        QVERIFY(!windows.contains(ConfigKeys::borderColorActiveKey()));
        QVERIFY(!windows.contains(ConfigKeys::borderColorInactiveKey()));
    }

    // ─── Decoration fields (show-border / radius / hide-title-bars) ────────

    void testDecorationFields_collapsedToConfig()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("ShowBorder"), true}, {QStringLiteral("Radius"), 12}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Decorations")},
                  {{QStringLiteral("HideTitleBars"), true}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject windows = windowsGroup();
        QCOMPARE(windows.value(ConfigKeys::showBorderKey()).toBool(), true);
        QCOMPARE(windows.value(ConfigKeys::radiusKey()).toInt(), 12);
        QCOMPARE(windows.value(ConfigKeys::hideTitleBarsKey()).toBool(), true);
        QCOMPARE(rules().size(), 0);
    }

    // Outer gap + per-side toggle + a per-side value collapse into the Gaps group.
    void testOuterAndPerSideGaps_collapsedToConfig()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Outer"), 20}, {QStringLiteral("UsePerSide"), true}, {QStringLiteral("Top"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject gaps = gapsGroup();
        QCOMPARE(gaps.value(ConfigKeys::outerGapKey()).toInt(), 20);
        QCOMPARE(gaps.value(ConfigKeys::usePerSideOuterGapKey()).toBool(), true);
        QCOMPARE(gaps.value(ConfigKeys::outerGapTopKey()).toInt(), 5);
    }

    // ─── Per-screen gaps → per-screen autotile config ──────────────────────

    // A v4 per-screen SNAPPING inner gap (stored under the legacy "ZonePadding"
    // key) collapses into the screen's per-screen autotile gap key
    // (AutotileInnerGap), and the gap-only PerScreen/Snapping subtree is dropped.
    void testPerScreenGap_snappingBecomesConfig()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("ZonePadding"), 16}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(rules().size(), 0);
        QCOMPARE(perScreenAutotile(QStringLiteral("DP-1")).value(QStringLiteral("AutotileInnerGap")).toInt(), 16);

        // The gap-only snapping per-screen subtree is fully consumed.
        const QJsonObject perScreen =
            readJson(ConfigDefaults::configFilePath()).value(QStringLiteral("PerScreen")).toObject();
        QVERIFY2(!perScreen.contains(QStringLiteral("Snapping")), "the PerScreen/Snapping subtree must be dropped");
    }

    // Snapping + autotile per-screen inner gap for the SAME monitor collapse onto
    // ONE per-screen autotile gap value. Both differ from the default, so the
    // tie-break prefers SNAPPING (16 wins over 24). Non-gap autotile keys survive.
    void testPerScreenGap_snappingAndAutotileMerge_survivorsPreserved()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("ZonePadding"), 16}});
        setNested(
            cfg, {QStringLiteral("PerScreen"), QStringLiteral("Autotile"), QStringLiteral("DP-1")},
            {{QStringLiteral("AutotileInnerGap"), 24}, {QStringLiteral("AutotileAlgorithm"), QStringLiteral("bsp")}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject autoScreen = perScreenAutotile(QStringLiteral("DP-1"));
        // Both differ from the compile default (8); the tie-break prefers snapping.
        QCOMPARE(autoScreen.value(QStringLiteral("AutotileInnerGap")).toInt(), 16);
        // The non-gap autotile per-screen key survives.
        QCOMPARE(autoScreen.value(QStringLiteral("AutotileAlgorithm")).toString(), QStringLiteral("bsp"));
        QCOMPARE(rules().size(), 0);
    }

    // A per-screen gap value EQUAL to the compile default is not written (the
    // differ-from-default contract), and the monitor is left without that key.
    void testPerScreenGap_defaultValueNotWritten()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("ZonePadding"), 8}}); // == default
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QVERIFY(!perScreenAutotile(QStringLiteral("DP-1")).contains(QStringLiteral("AutotileInnerGap")));
    }

    // ─── Consumed v4 groups removed, survivors preserved ──────────────────

    void testV4GroupsRemoved_survivorsPreserved()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        // Survivors: the snapping adjacency threshold and the tiling smart-gaps
        // flag share the per-mode Gaps groups but are NOT unified gap dimensions.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Inner"), 12}, {QStringLiteral("AdjacentThreshold"), 25}});
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Inner"), 12}, {QStringLiteral("SmartGaps"), true}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());

        // The consumed appearance sub-groups are gone.
        QVERIFY(!after.value(QStringLiteral("Snapping"))
                     .toObject()
                     .value(QStringLiteral("Appearance"))
                     .toObject()
                     .contains(QStringLiteral("Borders")));

        // Survivors preserved in their per-mode Gaps groups.
        QCOMPARE(after.value(QStringLiteral("Snapping"))
                     .toObject()
                     .value(QStringLiteral("Gaps"))
                     .toObject()
                     .value(QStringLiteral("AdjacentThreshold"))
                     .toInt(),
                 25);
        QCOMPARE(after.value(QStringLiteral("Tiling"))
                     .toObject()
                     .value(QStringLiteral("Gaps"))
                     .toObject()
                     .value(QStringLiteral("SmartGaps"))
                     .toBool(),
                 true);

        // The consumed gap dimension keys are gone from the per-mode Gaps groups.
        QVERIFY(!after.value(QStringLiteral("Snapping"))
                     .toObject()
                     .value(QStringLiteral("Gaps"))
                     .toObject()
                     .contains(QStringLiteral("Inner")));
    }

    // ─── Idempotency ──────────────────────────────────────────────────────

    void testIdempotency_runTwiceIsNoOp()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")}, {{QStringLiteral("Inner"), 12}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QJsonObject firstWindows = windowsGroup();
        const QJsonObject firstGaps = gapsGroup();
        const int firstRuleCount = rules().size();

        // Re-run against the now-v5 tree: no chain step runs, values unchanged.
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QCOMPARE(windowsGroup(), firstWindows);
        QCOMPARE(gapsGroup(), firstGaps);
        QCOMPARE(rules().size(), firstRuleCount);

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }
};

// NOT guiless: the migration chain constructs Settings during finalize, whose
// load reads QGuiApplication::palette().
QTEST_MAIN(TestMigrationV4ToV5)
#include "test_migration_v4_to_v5.moc"

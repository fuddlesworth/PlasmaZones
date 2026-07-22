// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4_exclusions.cpp
 * @brief v3 → v4 migration tests for the snapping-exclusion fold (the
 *        `Exclusions.{Applications,WindowClasses}` lists folding into
 *        `AppId AppIdMatches` Exclude rules) and the zone-overlay group
 *        rename (`Snapping.Appearance.*` → `Snapping.Zones.*`).
 *
 * Split out of test_migration_v3_to_v4.cpp; the shared config/rules JSON
 * helpers live in MigrationV3V4Fixture.h.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUuid>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"

#include "MigrationV3V4Fixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV3ToV4Exclusions : public QObject, public MigrationV3V4Fixture
{
    Q_OBJECT

    // ─── Exclusions fold ─────────────────────────────────────────────────
    // The legacy `Exclusions.{Applications,WindowClasses}` comma-joined
    // lists fold into Application-subject `AppId AppIdMatches <pattern>`
    // matchers with a terminal `Exclude` action — the same shape the
    // legacy runtime bridge produced for the daemon's navigation gates
    // (see git history for `ExclusionListBridge`), so an upgrading
    // user's runtime exclusion behaviour is preserved.

private:
    /// A v3 config carrying just an Exclusions group. No disable lists, no
    /// animation rules — keeps the assertion scope tight to the exclusion
    /// fold so failures point straight at the offending stash / rule
    /// builder.
    QJsonObject makeV3ConfigWithExclusions(const QString& apps, const QString& windowClasses)
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);
        QJsonObject exclusions;
        if (!apps.isNull()) {
            exclusions.insert(QStringLiteral("Applications"), apps);
        }
        if (!windowClasses.isNull()) {
            exclusions.insert(QStringLiteral("WindowClasses"), windowClasses);
        }
        if (!exclusions.isEmpty()) {
            root.insert(QStringLiteral("Exclusions"), exclusions);
        }
        return root;
    }

    /// Surface every rule that's an Application-subject Exclude rule (the
    /// only shape the exclusion fold produces). A v3 fixture with no
    /// assignments / disable lists / animation rules yields a rule set
    /// containing exactly the migrated exclusions + the premade Steam rule;
    /// the Steam rule matches on `windowClass`, not `appId`, so this filter
    /// targets the exclusion-fold output cleanly.
    QList<QJsonObject> exclusionRules(const QJsonArray& rules)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (actionTypes(r) != QStringList{QStringLiteral("exclude")}) {
                continue;
            }
            if (matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches")).isEmpty()) {
                continue;
            }
            out.append(r);
        }
        return out;
    }

    /// A v3 config carrying populated zone-overlay groups under the OLD
    /// Snapping.Appearance.* paths, plus a sibling Snapping.Behavior group so
    /// the prune step is exercised against a non-empty "Snapping" parent.
    QJsonObject makeV3ConfigWithZoneAppearance()
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);

        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), false);
        QJsonObject opacity;
        opacity.insert(QStringLiteral("Active"), 0.5);
        QJsonObject border;
        border.insert(QStringLiteral("Width"), 3);
        QJsonObject labels;
        labels.insert(QStringLiteral("FontFamily"), QStringLiteral("X"));

        QJsonObject appearance;
        appearance.insert(QStringLiteral("Colors"), colors);
        appearance.insert(QStringLiteral("Opacity"), opacity);
        appearance.insert(QStringLiteral("Border"), border);
        appearance.insert(QStringLiteral("Labels"), labels);

        // A sibling sub-group that must survive (and keep "Snapping" alive).
        QJsonObject behavior;
        behavior.insert(QStringLiteral("ToggleActivation"), true);

        QJsonObject snapping;
        snapping.insert(QStringLiteral("Appearance"), appearance);
        snapping.insert(QStringLiteral("Behavior"), behavior);
        root.insert(QStringLiteral("Snapping"), snapping);

        return root;
    }

private Q_SLOTS:

    void testExclusions_applicationsBecomeAppIdMatchesExcludeRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithExclusions(QStringLiteral("firefox,konsole"), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> excl = exclusionRules(rulesFromRules());
        QCOMPARE(excl.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : excl) {
            patterns.append(matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox") << QStringLiteral("konsole"));

        // Schema invariants spelled out so a regression in the builder
        // (e.g. wrong field, wrong op, multi-action rule) fails here rather
        // than at runtime in the rule evaluator.
        for (const QJsonObject& r : excl) {
            QVERIFY(r.value(QStringLiteral("enabled")).toBool());
            // assignBandPrioritiesToZeroRules seeds the simple AppId-match Exclude
            // rules in the Application band (200-299) so they display sensibly
            // instead of all reading "Priority 0".
            const int priority = r.value(QStringLiteral("priority")).toInt();
            QVERIFY(priority >= 200 && priority < 300);
        }
    }

    void testExclusions_windowClassesBecomeAppIdMatchesExcludeRules()
    {
        // The v3 schema split the two lists by intent (one matched against
        // desktopFileName, one against windowClass) but the daemon's runtime
        // bridge always folded BOTH against the resolved appId via the
        // segment-aware AppIdMatches operator. The migration preserves that
        // bridge-flavoured semantics — so a WindowClasses entry must produce
        // an AppId AppIdMatches leaf, NOT a WindowClass Contains leaf.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithExclusions(QString(), QStringLiteral("kitty,org.kde.dolphin")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> excl = exclusionRules(rulesFromRules());
        QCOMPARE(excl.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : excl) {
            patterns.append(matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("kitty") << QStringLiteral("org.kde.dolphin"));
    }

    void testExclusions_emptyAndWhitespacePatternsDropped()
    {
        // Empty (",,") and whitespace-only ("  ") entries used to slip into
        // the legacy bridge as never-matching rules. The migration mirrors
        // the bridge's trimmed-empty skip so the rule store doesn't grow
        // inert entries from the conversion.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithExclusions(QStringLiteral(",firefox,  ,,konsole,"), QStringLiteral("   ,,")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> excl = exclusionRules(rulesFromRules());
        // Only "firefox" and "konsole" survive — the empty / whitespace
        // entries from BOTH lists are dropped.
        QCOMPARE(excl.size(), 2);
    }

    void testExclusions_idempotentRuleIds()
    {
        // Mirrors the animation-rule idempotency case: re-running the
        // migration on the same v3 input produces byte-identical rule ids,
        // because the id is derived from `(Field::AppId,
        // Operator::AppIdMatches, pattern)` through a fixed v5-UUID
        // namespace. A regression in the namespace literal or the
        // length-prefixed segment encoding would break the dedup guarantee
        // the legacy runtime bridge relied on — see git history for the
        // pre-v4 `ExclusionListBridge` builder.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithExclusions(QStringLiteral("firefox"), QString()));
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId = exclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());

        // Golden assertion: re-derive the expected id inline against the
        // SPEC of the namespace UUID + length-prefixed segment encoding
        // (mirrors the equivalent guard on the animation rule test). The
        // migration owns both ends of the v5-UUID derivation, so the
        // round-trip idempotency check below cannot catch a namespace-UUID
        // or encoder change — both sides of that compare drift together.
        // Duplicating the namespace literal AND the segment-encoding
        // format here means a deliberate change to either forces a
        // deliberate update to this test.
        const QUuid kExpectedNamespace(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
        // Segment encoding: <size>:<bytes> per segment, no separator.
        //   field = static_cast<int>(Field::AppId) = 0 → "1:0"
        //   op    = static_cast<int>(Operator::AppIdMatches) = 5 → "1:5"
        //   pattern = "firefox" → "7:firefox"
        const QString kExpectedKey = QStringLiteral("1:0") + QStringLiteral("1:5") + QStringLiteral("7:firefox");
        const QString expectedId = QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString();
        QCOMPARE(firstId, expectedId);

        // Round-trip: wipe rules.json + re-stage same v3 input.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithExclusions(QStringLiteral("firefox"), QString()));
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString secondId = exclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QCOMPARE(secondId, firstId);
    }

    void testExclusions_groupAndStashStrippedAfterFinalize()
    {
        // The v3 Exclusions group AND the v4 scratch stash key MUST both be
        // gone from config.json after a successful conversion. A regression
        // in either site (migrateV3ToV4 forgetting to delete the source
        // group, or finalizeV4Conversion forgetting to strip the stash)
        // would leave inert keys on disk that the settings UI would
        // gladly resurface — re-introducing the exact duplication this
        // migration is supposed to retire.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithExclusions(QStringLiteral("firefox"), QStringLiteral("kitty")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("Exclusions")),
                 "migrateV3ToV4 must remove the legacy Exclusions group");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4ExclusionStash")),
                 "finalizeV4Conversion must strip the _v4ExclusionStash scratch key");
    }

    void testExclusions_absentSourceProducesNoExclusionRules()
    {
        // A clean v3 config with no Exclusions group must not produce any
        // exclusion stash entry and not contribute any exclusion rule to
        // the output. The premade Steam rule still appears — the assertion
        // targets the exclusion-shaped subset exactly.
        IsolatedConfigGuard guard;
        // makeV3ConfigWithExclusions with both nulls produces a v3 config
        // with NO Exclusions group at all.
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithExclusions(QString(), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(exclusionRules(rulesFromRules()).size(), 0);
    }

    // ─── Zone-overlay group rename: Snapping.Appearance.* → Snapping.Zones.* ─
    //
    // v3.1 renamed the "Snapping › Appearance" page (drag-time zone overlay)
    // to "Zones", moving its config groups so the freed Snapping.Appearance.*
    // namespace can hold the new snapped-window appearance settings. The move
    // folds into the EXISTING migrateV3ToV4 — no schema bump. These pins
    // assert the on-disk config.json shape; the four group/key names are
    // inline-literal pins so the test is an INDEPENDENT WITNESS of the rename.

    void testZoneRename_movesGroupsToZonesNamespace()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithZoneAppearance());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        // The migration chain now runs v3 → v4 → v5, so config.json lands at
        // the current schema version (the v3→v4 step still stamps 4 mid-chain).
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        const QJsonObject snapping = cfg.value(QStringLiteral("Snapping")).toObject();

        // The four zone sub-groups now live under Snapping.Zones.* with their
        // exact keys/values preserved.
        const QJsonObject zones = snapping.value(QStringLiteral("Zones")).toObject();
        QCOMPARE(zones.value(QStringLiteral("Colors")).toObject().value(QStringLiteral("UseSystem")).toBool(), false);
        QVERIFY(qFuzzyCompare(
            zones.value(QStringLiteral("Opacity")).toObject().value(QStringLiteral("Active")).toDouble(), 0.5));
        QCOMPARE(zones.value(QStringLiteral("Border")).toObject().value(QStringLiteral("Width")).toInt(), 3);
        QCOMPARE(zones.value(QStringLiteral("Labels")).toObject().value(QStringLiteral("FontFamily")).toString(),
                 QStringLiteral("X"));

        // The old Snapping.Appearance group is gone entirely — no husk lingers.
        QVERIFY2(!snapping.contains(QStringLiteral("Appearance")),
                 "the old Snapping.Appearance zone-overlay group must be removed by the rename");

        // The "Snapping" parent survives because Behavior still lives there.
        QVERIFY(snapping.contains(QStringLiteral("Behavior")));
        QCOMPARE(
            snapping.value(QStringLiteral("Behavior")).toObject().value(QStringLiteral("ToggleActivation")).toBool(),
            true);

        // End-state load check: a Settings loaded from the migrated config must
        // read the zone-overlay UseSystem flag from its new Snapping.Zones.Colors
        // home (the migration relocated the v3 UseSystem=false there), proving the
        // renamed group is wired to the live getter.
        Settings settings;
        QCOMPARE(settings.useSystemColors(), false);
    }

    void testZoneRename_absentSourceIsNoOp()
    {
        IsolatedConfigGuard guard;
        // A v3 config with no Snapping.Appearance group at all.
        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 3);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject snapping = after.value(QStringLiteral("Snapping")).toObject();
        // No Snapping.Zones group is fabricated from absent input.
        QVERIFY2(!snapping.contains(QStringLiteral("Zones")),
                 "absent Snapping.Appearance must NOT fabricate a Snapping.Zones group");
        QVERIFY(!snapping.contains(QStringLiteral("Appearance")));
    }

    void testZoneRename_v4VersionGatePreventsRemove()
    {
        IsolatedConfigGuard guard;

        // An already-v4 config whose zone groups are at the NEW Snapping.Zones.*
        // paths. The v4 version gate short-circuits migrateV3ToV4 entirely (the
        // move code is never reached), so the groups must not be moved or
        // duplicated and no Snapping.Appearance group is resurrected.
        QJsonObject zonesColors;
        zonesColors.insert(QStringLiteral("UseSystem"), true);
        QJsonObject zones;
        zones.insert(QStringLiteral("Colors"), zonesColors);
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Zones"), zones);
        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 4);
        cfg.insert(QStringLiteral("Snapping"), snapping);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject snappingAfter = after.value(QStringLiteral("Snapping")).toObject();
        // Still at Snapping.Zones, value intact, and no Snapping.Appearance
        // resurrected.
        QCOMPARE(snappingAfter.value(QStringLiteral("Zones"))
                     .toObject()
                     .value(QStringLiteral("Colors"))
                     .toObject()
                     .value(QStringLiteral("UseSystem"))
                     .toBool(),
                 true);
        QVERIFY(!snappingAfter.contains(QStringLiteral("Appearance")));
    }

    void testZoneRename_coexistsWithExclusionsAndDisableFold()
    {
        IsolatedConfigGuard guard;

        // A full v3 fixture (Display.*Disabled* + Snapping default + assignments)
        // ADDITIONALLY carrying the zone-overlay groups. The zone move must not
        // disturb the existing disable-list / assignment fold.
        QJsonObject cfg = makeV3Config();
        QJsonObject snapping = cfg.value(QStringLiteral("Snapping")).toObject();
        QJsonObject colors;
        colors.insert(QStringLiteral("UseSystem"), false);
        QJsonObject appearance;
        appearance.insert(QStringLiteral("Colors"), colors);
        snapping.insert(QStringLiteral("Appearance"), appearance);
        cfg.insert(QStringLiteral("Snapping"), snapping);
        writeJson(ConfigDefaults::configFilePath(), cfg);
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The existing disable-list fold still produces its 5 DisableEngine
        // rules (same assertion as testDisableListRules).
        const QList<QJsonObject> disabled = disableRules(rulesFromRules());
        QCOMPARE(disabled.size(), 5);

        // The Display group is still drained empty.
        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QVERIFY(!after.contains(QStringLiteral("Display")));

        // And the zone move landed alongside it.
        const QJsonObject snappingAfter = after.value(QStringLiteral("Snapping")).toObject();
        QVERIFY(!snappingAfter.contains(QStringLiteral("Appearance")));
        QCOMPARE(snappingAfter.value(QStringLiteral("Zones"))
                     .toObject()
                     .value(QStringLiteral("Colors"))
                     .toObject()
                     .value(QStringLiteral("UseSystem"))
                     .toBool(),
                 false);
    }
};

QTEST_MAIN(TestMigrationV3ToV4Exclusions)
#include "test_migration_v3_to_v4_exclusions.moc"

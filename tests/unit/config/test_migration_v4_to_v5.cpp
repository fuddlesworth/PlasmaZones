// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v4_to_v5.cpp
 * @brief Unit tests for the v4 → v5 schema migration (per-mode appearance +
 *        gaps → window-rule overrides).
 *
 * The v4 schema stored per-mode (separate Snapping vs Tiling) window
 * appearance (borders, title bars, colours) and gap settings in config.json,
 * plus per-screen gap subsets. This branch deleted those settings and routes
 * the same concerns through rules. A v4 config.json fixture (plus a
 * pre-existing v4 rules.json so finalizeV4Conversion takes its
 * cleanup-only branch, mirroring a real upgrade) is run through
 * ConfigMigration::ensureJsonConfig; the test asserts:
 *   - a clean/default v4 config migrates to ZERO override rules,
 *   - a customised Snapping border width + Tiling inner gap produce a
 *     "Snapping appearance" rule (Mode=snapping) carrying only SetBorderWidth
 *     and a "Tiling gaps" rule (Mode=tiling) carrying only SetInnerGap (a stash
 *     with only gap actions is named "<Mode> gaps", not "<Mode> appearance"),
 *   - use-system-border-colours=false + custom hex → SetBorderColorActive /
 *     SetBorderColorInactive carry the hex; =true → no colour action,
 *   - a per-screen gap override becomes a ScreenId-matched gap rule keyed by
 *     the same deterministic id WindowAppearanceController::perScreenGapRuleId
 *     derives,
 *   - the conversion is idempotent (running twice does not duplicate rules and
 *     leaves no _v5AppearanceStash),
 *   - the consumed v4 groups are removed from config.json while surviving
 *     non-gap keys (AdjacentThreshold / SmartGaps) are preserved,
 *   - config.json is stamped at the current schema version.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUuid>

#include "../../../src/config/configdefaults.h"
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

    /// Seed a valid, empty v4 rules.json so finalizeV4Conversion takes
    /// its cleanup-only branch (the rule store already parses as v4) instead of
    /// rebuilding — exactly what a real v4→v5 upgrade encounters, where the
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

    QJsonObject ruleByName(const QString& name)
    {
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("name")).toString() == name) {
                return r;
            }
        }
        return {};
    }

    /// Sorted action `type` strings of a rule.
    QStringList actionTypes(const QJsonObject& rule)
    {
        QStringList types;
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            types.append(v.toObject().value(QStringLiteral("type")).toString());
        }
        types.sort();
        return types;
    }

    /// The `value` of the first action whose `type` matches.
    QJsonValue actionValue(const QJsonObject& rule, const QString& type)
    {
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("type")).toString() == type) {
                return a.value(QStringLiteral("value"));
            }
        }
        return {};
    }

    /// A bare equality leaf reads {field, op, value}. Returns the match object.
    QJsonObject matchOf(const QJsonObject& rule)
    {
        return rule.value(QStringLiteral("match")).toObject();
    }

    /// A v4 config root carrying a nested group at the given dot-path with the
    /// supplied key/value pairs merged in.
    void setNested(QJsonObject& root, const QStringList& segments, const QJsonObject& keys)
    {
        // Build bottom-up so existing siblings survive.
        QList<QJsonObject> chain;
        QJsonObject node = root;
        for (int i = 0; i < segments.size(); ++i) {
            chain.append(node);
            node = node.value(segments.at(i)).toObject();
        }
        // node is the leaf group; merge keys.
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            node.insert(it.key(), it.value());
        }
        // Fold back up.
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

private Q_SLOTS:

    // ─── Clean default v4 → zero override rules ────────────────────────────

    void testCleanDefaultV4_producesNoOverrideRules()
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

        // No override rules were added — the seeded store stays empty.
        QCOMPARE(rules().size(), 0);

        // Config stamped at the current schema version, stash gone.
        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(after.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
        QVERIFY(!after.contains(QStringLiteral("_v5AppearanceStash")));
    }

    // ─── Customised border width + inner gap ──────────────────────────────

    void testCustomBorderWidthAndInnerGap_perModeRules()
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

        // "Snapping appearance" rule: Mode=snapping, only SetBorderWidth=5.
        const QJsonObject snap = ruleByName(QStringLiteral("Snapping appearance"));
        QVERIFY2(!snap.isEmpty(), "a Snapping appearance override rule must exist");
        QVERIFY(!snap.value(QStringLiteral("managed")).toBool());
        QCOMPARE(actionTypes(snap), (QStringList{QStringLiteral("setBorderWidth")}));
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderWidth")).toInt(), 5);
        const QJsonObject snapMatch = matchOf(snap);
        QCOMPARE(snapMatch.value(QStringLiteral("field")).toString(), QStringLiteral("mode"));
        QCOMPARE(snapMatch.value(QStringLiteral("op")).toString(), QStringLiteral("equals"));
        QCOMPARE(snapMatch.value(QStringLiteral("value")).toString(), QStringLiteral("snapping"));

        // "Tiling gaps" rule: Mode=tiling, only SetInnerGap=12. A stash carrying
        // only gap actions is named "<Mode> gaps", not "<Mode> appearance".
        const QJsonObject tiling = ruleByName(QStringLiteral("Tiling gaps"));
        QVERIFY2(!tiling.isEmpty(), "a Tiling gaps override rule must exist");
        QCOMPARE(actionTypes(tiling), (QStringList{QStringLiteral("setInnerGap")}));
        QCOMPARE(actionValue(tiling, QStringLiteral("setInnerGap")).toInt(), 12);
        const QJsonObject tilingMatch = matchOf(tiling);
        QCOMPARE(tilingMatch.value(QStringLiteral("field")).toString(), QStringLiteral("mode"));
        QCOMPARE(tilingMatch.value(QStringLiteral("value")).toString(), QStringLiteral("tiling"));

        // The re-pointed rules must load as context-only + valid: a Mode match is
        // a context field, so the gap action on the tiling rule resolves through
        // the gap cascade (the whole point of moving off IsTiled, a window-
        // property field on which gap actions were inert).
        const auto setOpt = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(setOpt.has_value());
        bool sawTiling = false;
        for (const PhosphorRules::Rule& r : setOpt->rules()) {
            if (r.name != QStringLiteral("Tiling gaps")) {
                continue;
            }
            sawTiling = true;
            QVERIFY2(r.match.isContextOnly(), "the Mode-matched tiling rule must be context-only");
            QVERIFY2(r.isValid(), "the re-pointed tiling rule must be valid");
            bool carriesGap = false;
            for (const PhosphorRules::RuleAction& a : r.actions) {
                if (a.type == QLatin1String(PhosphorRules::ActionType::SetInnerGap)) {
                    carriesGap = true;
                }
            }
            QVERIFY2(carriesGap, "the tiling rule must carry its gap action");
        }
        QVERIFY(sawTiling);

        // Exactly those two override rules.
        QCOMPARE(rules().size(), 2);
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

        const QJsonObject snap = ruleByName(QStringLiteral("Snapping appearance"));
        QVERIFY2(!snap.isEmpty(), "useSystem=false must produce a Snapping appearance rule");
        QCOMPARE(actionTypes(snap),
                 (QStringList{QStringLiteral("setBorderColorActive"), QStringLiteral("setBorderColorInactive")}));
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderColorActive")).toString(), QStringLiteral("#ffff0000"));
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderColorInactive")).toString(), QStringLiteral("#ff00ff00"));
    }

    void testColors_useSystemTrue_noColorAction()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // useSystem=true is the v4 default (accent), so it contributes nothing.
        // A differing border width keeps the rule alive so we can assert the
        // ABSENCE of any colour action rather than the absence of the rule.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Colors")},
                  {{QStringLiteral("UseSystem"), true},
                   {QStringLiteral("Active"), QStringLiteral("#ffff0000")},
                   {QStringLiteral("Inactive"), QStringLiteral("#ff00ff00")}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject snap = ruleByName(QStringLiteral("Snapping appearance"));
        QVERIFY(!snap.isEmpty());
        // useSystem=true is the accent default — the only action is the
        // differing border width; no colour action is carried.
        QCOMPARE(actionTypes(snap), (QStringList{QStringLiteral("setBorderWidth")}));
    }

    // ─── Per-screen gaps ──────────────────────────────────────────────────

    void testPerScreenGap_becomesScreenIdRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // A snapping per-screen inner-gap override on DP-1 (16 != default 8).
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("InnerGap"), 16}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The rule id matches WindowAppearanceController::perScreenGapRuleId.
        const QString expectedId =
            QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), QByteArrayLiteral("DP-1")).toString();
        QJsonObject screenRule;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == expectedId) {
                screenRule = r;
            }
        }
        QVERIFY2(!screenRule.isEmpty(), "per-screen gap must become a ScreenId-keyed rule with the derived id");
        QVERIFY(!screenRule.value(QStringLiteral("managed")).toBool());
        QCOMPARE(actionTypes(screenRule), (QStringList{QStringLiteral("setInnerGap")}));
        QCOMPARE(actionValue(screenRule, QStringLiteral("setInnerGap")).toInt(), 16);
        const QJsonObject m = matchOf(screenRule);
        QCOMPARE(m.value(QStringLiteral("field")).toString(), QStringLiteral("screenId"));
        QCOMPARE(m.value(QStringLiteral("op")).toString(), QStringLiteral("equals"));
        QCOMPARE(m.value(QStringLiteral("value")).toString(), QStringLiteral("DP-1"));
    }

    // ─── Consumed v4 groups removed, survivors preserved ──────────────────

    void testV4GroupsRemoved_survivorsPreserved()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        // Snapping.Gaps carries a deleted gap key AND the surviving
        // AdjacentThreshold; only the gap key must be stripped.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Inner"), 12}, {QStringLiteral("AdjacentThreshold"), 24}});
        // Tiling.Gaps carries a deleted gap key AND the surviving SmartGaps.
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Outer"), 20}, {QStringLiteral("SmartGaps"), true}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        const QJsonObject snapping = after.value(QStringLiteral("Snapping")).toObject();
        const QJsonObject tiling = after.value(QStringLiteral("Tiling")).toObject();

        // The whole appearance subtree is gone.
        QVERIFY2(!snapping.contains(QStringLiteral("Appearance")), "Snapping.Appearance must be removed");

        // The deleted gap keys are gone; the survivors stay.
        const QJsonObject snapGaps = snapping.value(QStringLiteral("Gaps")).toObject();
        QVERIFY(!snapGaps.contains(QStringLiteral("Inner")));
        QCOMPARE(snapGaps.value(QStringLiteral("AdjacentThreshold")).toInt(), 24);
        const QJsonObject tilingGaps = tiling.value(QStringLiteral("Gaps")).toObject();
        QVERIFY(!tilingGaps.contains(QStringLiteral("Outer")));
        QCOMPARE(tilingGaps.value(QStringLiteral("SmartGaps")).toBool(), true);

        // Stash stripped after a successful conversion.
        QVERIFY(!after.contains(QStringLiteral("_v5AppearanceStash")));
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
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        const int firstCount = rules().size();
        QCOMPARE(firstCount, 2);

        // Re-run against the now-v5 tree: no chain step runs, finalizeV5 sees
        // no stash and no-ops. rules.json is byte-identical.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(secondRun, firstRun);
        QCOMPARE(rules().size(), firstCount);

        const QJsonObject after = readJson(ConfigDefaults::configFilePath());
        QVERIFY(!after.contains(QStringLiteral("_v5AppearanceStash")));
    }
};

QTEST_MAIN(TestMigrationV4ToV5)
#include "test_migration_v4_to_v5.moc"

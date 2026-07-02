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
#include "../../../src/config/settings.h"
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

    // ─── Decoration fields (show-border / radius / hide-title-bars) ────────

    void testDecorationFields_perModeRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Snapping: show-border on (default off) and border radius 12 (default 8)
        // live in the Borders group; hide-title-bars on (default off) lives in
        // the Decorations group. These three are the headline "appearance to
        // rules" decoration fields and were previously untested.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("ShowBorder"), true}, {QStringLiteral("Radius"), 12}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Decorations")},
                  {{QStringLiteral("HideTitleBars"), true}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The three decoration fields become SetBorderVisible / SetBorderRadius /
        // SetHideTitleBar on the Mode=snapping appearance rule.
        const QJsonObject snap = ruleByName(QStringLiteral("Snapping appearance"));
        QVERIFY2(!snap.isEmpty(), "a Snapping appearance override rule must exist");
        QVERIFY(!snap.value(QStringLiteral("managed")).toBool());
        QCOMPARE(actionTypes(snap),
                 (QStringList{QStringLiteral("setBorderRadius"), QStringLiteral("setBorderVisible"),
                              QStringLiteral("setHideTitleBar")}));
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderVisible")).toBool(), true);
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderRadius")).toInt(), 12);
        QCOMPARE(actionValue(snap, QStringLiteral("setHideTitleBar")).toBool(), true);
        const QJsonObject snapMatch = matchOf(snap);
        QCOMPARE(snapMatch.value(QStringLiteral("field")).toString(), QStringLiteral("mode"));
        QCOMPARE(snapMatch.value(QStringLiteral("value")).toString(), QStringLiteral("snapping"));

        // Exactly the one appearance rule.
        QCOMPARE(rules().size(), 1);
    }

    void testOutOfRangeValues_clampedNotDropped()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // A v4 config (hand-edited, or from a wider-range older slider) carrying
        // values ABOVE the rule-action validator maxima. The migration must CLAMP
        // them (border width→10, radius→20, gap→500), not emit them verbatim:
        // an out-of-range action fails Rule::isValid(), addRule drops the whole
        // rule, and the stash is stripped, permanently losing every override for
        // that mode. This test fails if the qBound clamp in actionsFromFields is
        // removed (the rule would vanish and rules().size() would be 0).
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 999}, {QStringLiteral("Radius"), 99}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")}, {{QStringLiteral("Inner"), 9999}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject snap = ruleByName(QStringLiteral("Snapping appearance"));
        QVERIFY2(!snap.isEmpty(), "the rule must survive — clamped, not dropped");
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderWidth")).toInt(), 10);
        QCOMPARE(actionValue(snap, QStringLiteral("setBorderRadius")).toInt(), 20);
        QCOMPARE(actionValue(snap, QStringLiteral("setInnerGap")).toInt(), 500);
        // The rule loads as valid (clamped values are inside the validator range).
        const auto setOpt = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(setOpt.has_value());
        for (const PhosphorRules::Rule& r : setOpt->rules()) {
            QVERIFY2(r.isValid(), "the clamped rule must be valid");
        }
    }

    // Outer gap + per-side toggle + a per-side value (defaults: outer 8,
    // usePerSide false). These outer/per-side fields share the field→action
    // mapping with inner gap but were previously untested, so a mis-mapping of
    // any of them would have shipped undetected.
    void testOuterAndPerSideGaps_perModeRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Gaps")},
                  {{QStringLiteral("Outer"), 20}, {QStringLiteral("UsePerSide"), true}, {QStringLiteral("Top"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject snap = ruleByName(QStringLiteral("Snapping gaps"));
        QVERIFY2(!snap.isEmpty(), "a Snapping gaps rule must exist");
        QVERIFY(!snap.value(QStringLiteral("managed")).toBool());
        QCOMPARE(actionValue(snap, QStringLiteral("setOuterGap")).toInt(), 20);
        QCOMPARE(actionValue(snap, QStringLiteral("setUsePerSideOuterGap")).toBool(), true);
        QCOMPARE(actionValue(snap, QStringLiteral("setOuterGapTop")).toInt(), 5);
    }

    // ─── Per-screen gaps ──────────────────────────────────────────────────

    void testPerScreenGap_becomesScreenIdRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // A snapping per-screen inner-gap override on DP-1 (16 != default 8). v4
        // persisted the per-screen snapping inner gap under the legacy on-disk key
        // "ZonePadding" (renamed to "InnerGap" in v5), so the fixture must use the
        // historical spelling — otherwise this test silently passes against a
        // migration that never reads the real upgrade data.
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("ZonePadding"), 16}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The rule id must be derived the SAME way the settings UI derives it
        // (WindowAppearanceController::perScreenGapRuleId), i.e. seeded from the
        // canonical per-screen key — route the expected id through the very same
        // contract function so a divergence between the migration and the
        // controller's key derivation is caught here (orphaned/duplicate rules).
        const QString canonicalKey = Settings::canonicalPerScreenKey(QStringLiteral("DP-1"));
        const QString expectedId =
            QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), canonicalKey.toUtf8()).toString();
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
        // The match value is the canonical key, matching the id derivation and the
        // runtime ScreenId the daemon resolves against.
        QCOMPARE(m.value(QStringLiteral("value")).toString(), canonicalKey);
    }

    // The snapping and autotile per-screen gap sets for the SAME monitor collapse
    // onto ONE ScreenId rule; autotile is processed second so its value wins on a
    // conflicting slot. Non-gap autotile per-screen keys (algorithm, etc.) are NOT
    // gaps and must survive in the config rather than being swept into the rule.
    void testPerScreenGap_snappingAndAutotileMerge_survivorsPreserved()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Snapping per-screen inner gap 16 (legacy on-disk key "ZonePadding");
        // autotile per-screen inner gap 24 (key "AutotileInnerGap") for the SAME
        // screen — a conflict on the same gap slot — plus a non-gap autotile key
        // ("Algorithm").
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Snapping"), QStringLiteral("DP-1")},
                  {{QStringLiteral("ZonePadding"), 16}});
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Autotile"), QStringLiteral("DP-1")},
                  {{QStringLiteral("AutotileInnerGap"), 24}, {QStringLiteral("Algorithm"), QStringLiteral("bsp")}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString canonicalKey = Settings::canonicalPerScreenKey(QStringLiteral("DP-1"));
        const QString expectedId =
            QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), canonicalKey.toUtf8()).toString();
        QJsonObject screenRule;
        int screenRuleCount = 0;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == expectedId) {
                screenRule = r;
                ++screenRuleCount;
            }
        }
        // Exactly ONE merged ScreenId rule (not one per mode).
        QCOMPARE(screenRuleCount, 1);
        QVERIFY2(!screenRule.isEmpty(), "the merged per-screen gap rule must exist");
        // Autotile processed second → its InnerGap (24) wins the conflicting slot.
        QCOMPARE(actionValue(screenRule, QStringLiteral("setInnerGap")).toInt(), 24);

        // The non-gap autotile per-screen key survives in the config (only gap
        // keys are swept into the rule).
        const QJsonObject autoScreen = readJson(ConfigDefaults::configFilePath())
                                           .value(QStringLiteral("PerScreen"))
                                           .toObject()
                                           .value(QStringLiteral("Autotile"))
                                           .toObject()
                                           .value(QStringLiteral("DP-1"))
                                           .toObject();
        QCOMPARE(autoScreen.value(QStringLiteral("Algorithm")).toString(), QStringLiteral("bsp"));
        QVERIFY2(!autoScreen.contains(QStringLiteral("AutotileInnerGap")),
                 "the gap key must be stripped from the config");
    }

    // Per-screen tiling geometry (split ratio / master count / max windows) AND the
    // per-screen Algorithm fold onto a dedicated per-monitor ScreenId rule,
    // namespaced under perScreenTilingRuleNamespaceId (DISTINCT from the gap
    // namespace) so the settings UI find-or-creates the same rule. The Algorithm
    // becomes a SetTilingAlgorithm action (the dedup); only behavioural flags
    // survive in the config.
    void testPerScreenTiling_becomesScreenIdRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Differing-from-default split ratio (0.66 != 0.5), master count (2 != 1),
        // max windows (3 != 5), plus a per-screen Algorithm ("AutotileAlgorithm" is
        // the prefixed disk spelling) that folds into a SetTilingAlgorithm action.
        // AutotileInsertPosition is a behavioural key that STAYS live — seeding it
        // proves the folded keys were stripped individually, not the whole
        // subtree dropped.
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Autotile"), QStringLiteral("DP-1")},
                  {{QStringLiteral("AutotileSplitRatio"), 0.66},
                   {QStringLiteral("AutotileMasterCount"), 2},
                   {QStringLiteral("AutotileMaxWindows"), 3},
                   {QStringLiteral("AutotileAlgorithm"), QStringLiteral("bsp")},
                   {QStringLiteral("AutotileInsertPosition"), 1}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString canonicalKey = Settings::canonicalPerScreenKey(QStringLiteral("DP-1"));
        const QString expectedId =
            QUuid::createUuidV5(ConfigDefaults::perScreenTilingRuleNamespaceId(), canonicalKey.toUtf8()).toString();
        // The tiling namespace must be distinct from the gap namespace, so the
        // tiling rule never collides with a per-screen gap rule for the same monitor.
        const QString gapId =
            QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), canonicalKey.toUtf8()).toString();
        QVERIFY2(expectedId != gapId, "tiling and gap per-screen rule ids must not collide");

        QJsonObject tilingRule;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == expectedId) {
                tilingRule = r;
            }
        }
        QVERIFY2(!tilingRule.isEmpty(), "per-screen tiling geometry must become a ScreenId-keyed rule");
        QVERIFY(!tilingRule.value(QStringLiteral("managed")).toBool());
        // All three geometry actions present; the default-valued companion is absent.
        const QStringList types = actionTypes(tilingRule);
        QVERIFY(types.contains(QStringLiteral("setSplitRatio")));
        QVERIFY(types.contains(QStringLiteral("setMasterCount")));
        QVERIFY(types.contains(QStringLiteral("setMaxWindows")));
        QVERIFY(types.contains(QStringLiteral("setTilingAlgorithm")));
        QCOMPARE(actionValue(tilingRule, QStringLiteral("setSplitRatio")).toDouble(), 0.66);
        QCOMPARE(actionValue(tilingRule, QStringLiteral("setMasterCount")).toInt(), 2);
        QCOMPARE(actionValue(tilingRule, QStringLiteral("setMaxWindows")).toInt(), 3);
        // SetTilingAlgorithm carries its token under the `algorithm` param, not `value`.
        QString foldedAlgo;
        for (const QJsonValue& av : tilingRule.value(QStringLiteral("actions")).toArray()) {
            if (av.toObject().value(QStringLiteral("type")).toString() == QLatin1String("setTilingAlgorithm"))
                foldedAlgo = av.toObject().value(QStringLiteral("algorithm")).toString();
        }
        QCOMPARE(foldedAlgo, QStringLiteral("bsp"));
        const QJsonObject m = matchOf(tilingRule);
        QCOMPARE(m.value(QStringLiteral("field")).toString(), QStringLiteral("screenId"));
        QCOMPARE(m.value(QStringLiteral("op")).toString(), QStringLiteral("equals"));
        QCOMPARE(m.value(QStringLiteral("value")).toString(), canonicalKey);

        // The geometry keys AND the Algorithm key are stripped from the config.
        const QJsonObject autoScreen = readJson(ConfigDefaults::configFilePath())
                                           .value(QStringLiteral("PerScreen"))
                                           .toObject()
                                           .value(QStringLiteral("Autotile"))
                                           .toObject()
                                           .value(QStringLiteral("DP-1"))
                                           .toObject();
        // The surviving behavioural key proves the entry itself is intact — the
        // !contains checks below are testing real per-key strips, not a
        // vanished subtree that would make them vacuously true.
        QCOMPARE(autoScreen.value(QStringLiteral("AutotileInsertPosition")).toInt(), 1);
        QVERIFY2(!autoScreen.contains(QStringLiteral("AutotileAlgorithm")),
                 "per-screen algorithm key must be stripped");
        QVERIFY2(!autoScreen.contains(QStringLiteral("AutotileSplitRatio")), "split ratio key must be stripped");
        QVERIFY2(!autoScreen.contains(QStringLiteral("AutotileMasterCount")), "master count key must be stripped");
        QVERIFY2(!autoScreen.contains(QStringLiteral("AutotileMaxWindows")), "max windows key must be stripped");
    }

    // A per-screen tiling entry entirely at v4 defaults stashes nothing and
    // produces no tiling rule (mirrors the clean-default appearance guarantee).
    void testPerScreenTiling_cleanDefaults_noRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Autotile"), QStringLiteral("DP-1")},
                  {{QStringLiteral("AutotileSplitRatio"), 0.5},
                   {QStringLiteral("AutotileMasterCount"), 1},
                   {QStringLiteral("AutotileMaxWindows"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString canonicalKey = Settings::canonicalPerScreenKey(QStringLiteral("DP-1"));
        const QString tilingId =
            QUuid::createUuidV5(ConfigDefaults::perScreenTilingRuleNamespaceId(), canonicalKey.toUtf8()).toString();
        for (const QJsonValue& v : rules()) {
            QVERIFY2(v.toObject().value(QStringLiteral("id")).toString() != tilingId,
                     "a default-valued per-screen tiling entry must not produce a rule");
        }
    }

    // Per-screen zone-selector overrides fold onto a dedicated per-monitor
    // ScreenId rule carrying one generic SetZoneSelectorProperty action per
    // differing property; default-valued properties contribute no action and the
    // ZoneSelector config category is removed wholesale.
    void testPerScreenZoneSelector_becomesScreenIdRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Position 4 (!= default 1), GridColumns 3 (!= 5), PreviewLockAspect false
        // (!= true) differ and fold; MaxRows 4 (== default) must contribute nothing.
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("ZoneSelector"), QStringLiteral("DP-1")},
                  {{QStringLiteral("Position"), 4},
                   {QStringLiteral("GridColumns"), 3},
                   {QStringLiteral("PreviewLockAspect"), false},
                   {QStringLiteral("MaxRows"), 4}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString canonicalKey = Settings::canonicalPerScreenKey(QStringLiteral("DP-1"));
        const QString expectedId =
            QUuid::createUuidV5(ConfigDefaults::perScreenZoneSelectorRuleNamespaceId(), canonicalKey.toUtf8())
                .toString();
        QJsonObject zsRule;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == expectedId) {
                zsRule = r;
            }
        }
        QVERIFY2(!zsRule.isEmpty(), "per-screen zone-selector overrides must become a ScreenId-keyed rule");
        QVERIFY(!zsRule.value(QStringLiteral("managed")).toBool());

        // Look up a SetZoneSelectorProperty action's value by its property token.
        const auto zsValue = [&zsRule](const QString& property) -> QJsonValue {
            for (const QJsonValue& av : zsRule.value(QStringLiteral("actions")).toArray()) {
                const QJsonObject a = av.toObject();
                if (a.value(QStringLiteral("type")).toString() == QLatin1String("setZoneSelectorProperty")
                    && a.value(QStringLiteral("property")).toString() == property) {
                    return a.value(QStringLiteral("value"));
                }
            }
            return QJsonValue(QJsonValue::Undefined);
        };
        QCOMPARE(zsValue(QStringLiteral("Position")).toInt(), 4);
        QCOMPARE(zsValue(QStringLiteral("GridColumns")).toInt(), 3);
        QCOMPARE(zsValue(QStringLiteral("PreviewLockAspect")).toBool(true), false);
        QVERIFY2(zsValue(QStringLiteral("MaxRows")).isUndefined(),
                 "a default-valued property must not produce an action");

        const QJsonObject m = matchOf(zsRule);
        QCOMPARE(m.value(QStringLiteral("field")).toString(), QStringLiteral("screenId"));
        QCOMPARE(m.value(QStringLiteral("value")).toString(), canonicalKey);

        // The whole ZoneSelector per-screen category is folded out of config.
        const QJsonObject perScreen =
            readJson(ConfigDefaults::configFilePath()).value(QStringLiteral("PerScreen")).toObject();
        QVERIFY2(!perScreen.contains(QStringLiteral("ZoneSelector")),
                 "the ZoneSelector category must be removed from config");
    }

    // The two animation min-size window-filter knobs fold onto per-axis MANAGED
    // baseline ExcludeAnimations rules whose match carries the threshold; a 0
    // (off — the default) value produces no rule (the daemon seeds the disabled
    // baseline), and the boolean toggles in the same group survive.
    void testAnimationMinSize_becomesExcludeAnimationsRules()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Width 300 folds; Height 0 (off) does not. A boolean toggle in the same
        // group must survive (only the min-size knobs fold).
        setNested(cfg, {QStringLiteral("Animations"), QStringLiteral("WindowFiltering")},
                  {{QStringLiteral("MinimumWindowWidth"), 300},
                   {QStringLiteral("MinimumWindowHeight"), 0},
                   {QStringLiteral("ExcludeTransientWindows"), true}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString widthId = ConfigDefaults::animationMinWidthRuleId().toString();
        const QString heightId = ConfigDefaults::animationMinHeightRuleId().toString();
        QJsonObject widthRule;
        bool sawHeightRule = false;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == widthId) {
                widthRule = r;
            }
            if (r.value(QStringLiteral("id")).toString() == heightId) {
                sawHeightRule = true;
            }
        }
        QVERIFY2(!widthRule.isEmpty(), "min-width must become an ExcludeAnimations rule");
        QVERIFY2(!sawHeightRule, "a 0 (off) min-height must NOT produce a rule (daemon seeds it)");
        QVERIFY2(widthRule.value(QStringLiteral("managed")).toBool(),
                 "the animation min-size baseline must be managed");
        QCOMPARE(actionTypes(widthRule), (QStringList{QStringLiteral("excludeAnimations")}));
        const QJsonObject m = matchOf(widthRule);
        QCOMPARE(m.value(QStringLiteral("field")).toString(), QStringLiteral("width"));
        QCOMPARE(m.value(QStringLiteral("op")).toString(), QStringLiteral("lessThan"));
        QCOMPARE(m.value(QStringLiteral("value")).toInt(), 300);

        // The min-size key is stripped; the boolean toggle survives in config.
        const QJsonObject wf = readJson(ConfigDefaults::configFilePath())
                                   .value(QStringLiteral("Animations"))
                                   .toObject()
                                   .value(QStringLiteral("WindowFiltering"))
                                   .toObject();
        QVERIFY2(!wf.contains(QStringLiteral("MinimumWindowWidth")), "min-width key must be stripped");
        QCOMPARE(wf.value(QStringLiteral("ExcludeTransientWindows")).toBool(), true);
    }

    // The two general (window-management) min-size knobs fold onto MANAGED baseline
    // Exclude rules whose match carries the threshold. Unlike the animation min-size
    // (default 0 = off), these are on-by-default (200/150): only a DIFFERING value
    // migrates (a config at default needs no rule — the daemon seeds the baseline);
    // the TransientWindows toggle in the same group survives.
    void testGeneralMinSize_becomesManagedExcludeBaselines()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        // Width 300 differs from the 200 default → migrates. Height 150 IS the
        // default → no rule (daemon seeds it). The transient toggle must survive.
        setNested(cfg, {QStringLiteral("Exclusions")},
                  {{QStringLiteral("MinimumWindowWidth"), 300},
                   {QStringLiteral("MinimumWindowHeight"), 150},
                   {QStringLiteral("TransientWindows"), false}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString widthId = ConfigDefaults::generalMinWidthRuleId().toString();
        const QString heightId = ConfigDefaults::generalMinHeightRuleId().toString();
        QJsonObject widthRule;
        bool sawHeightRule = false;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == widthId) {
                widthRule = r;
            }
            if (r.value(QStringLiteral("id")).toString() == heightId) {
                sawHeightRule = true;
            }
        }
        QVERIFY2(!widthRule.isEmpty(), "a differing min-width must become a managed Exclude baseline");
        QVERIFY2(!sawHeightRule, "a default (150) min-height must NOT migrate (daemon seeds it)");
        QVERIFY2(widthRule.value(QStringLiteral("managed")).toBool(), "the min-size baseline must be managed");
        QCOMPARE(actionTypes(widthRule), (QStringList{QStringLiteral("exclude")}));
        const QJsonObject m = matchOf(widthRule);
        QCOMPARE(m.value(QStringLiteral("field")).toString(), QStringLiteral("width"));
        QCOMPARE(m.value(QStringLiteral("op")).toString(), QStringLiteral("lessThan"));
        QCOMPARE(m.value(QStringLiteral("value")).toInt(), 300);

        // The min-size key is stripped; the transient toggle survives in config.
        const QJsonObject excl =
            readJson(ConfigDefaults::configFilePath()).value(QStringLiteral("Exclusions")).toObject();
        QVERIFY2(!excl.contains(QStringLiteral("MinimumWindowWidth")), "min-width key must be stripped");
        QCOMPARE(excl.value(QStringLiteral("TransientWindows")).toBool(), false);
    }

    // A disabled general min-size (0) still migrates — as a managed baseline whose
    // match is Width/Height LessThan 0 (never matches), so the daemon's default-seed
    // (on) doesn't silently re-enable an axis the user turned off.
    void testGeneralMinSize_disabledMigratesAsZeroThresholdBaseline()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Exclusions")}, {{QStringLiteral("MinimumWindowWidth"), 0}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString widthId = ConfigDefaults::generalMinWidthRuleId().toString();
        QJsonObject widthRule;
        for (const QJsonValue& v : rules()) {
            if (v.toObject().value(QStringLiteral("id")).toString() == widthId)
                widthRule = v.toObject();
        }
        QVERIFY2(!widthRule.isEmpty(), "a disabled (0) min-width must still migrate to override the default seed");
        QVERIFY(widthRule.value(QStringLiteral("managed")).toBool());
        QCOMPARE(matchOf(widthRule).value(QStringLiteral("value")).toInt(), 0);
    }

    // finalizeV5 defer path: if rules.json is unreadable/corrupt when the
    // override rules are merged in, the conversion must DEFER — return false and
    // leave `_v5AppearanceStash` in place so a later run (after rules.json is
    // re-established) retries, rather than stripping the stash and permanently
    // losing the user's appearance/gap overrides. This guards the data-loss
    // failure mode of finalizeV5Conversion's load-failure branch.
    // The seven zone-overlay appearance values fold onto the MANAGED baseline
    // overlay rule: colours as #AARRGGBB hex, opacities clamped to [0,1],
    // border ints clamped to their validator bounds. The source config keys
    // are stripped from all three Snapping.Zones sub-groups while the
    // UseSystem toggle (still config) survives.
    void testOverlayAppearance_becomesManagedBaselineOverlayRule()
    {
        IsolatedConfigGuard guard;
        seedEmptyRules();

        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Zones"), QStringLiteral("Colors")},
                  {{QStringLiteral("UseSystem"), true},
                   {QStringLiteral("Highlight"), QStringLiteral("#FF112233")},
                   {QStringLiteral("Inactive"), QStringLiteral("#80445566")},
                   {QStringLiteral("Border"), QStringLiteral("#FF778899")}});
        // Out-of-range opacity (1.7) and border width (99) prove the clamps.
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Zones"), QStringLiteral("Opacity")},
                  {{QStringLiteral("Active"), 1.7}, {QStringLiteral("Inactive"), 0.25}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Zones"), QStringLiteral("Border")},
                  {{QStringLiteral("Width"), 99}, {QStringLiteral("Radius"), 12}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString overlayId = ConfigDefaults::baselineOverlayRuleId().toString();
        QJsonObject overlayRule;
        for (const QJsonValue& v : rules()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == overlayId) {
                overlayRule = r;
            }
        }
        QVERIFY2(!overlayRule.isEmpty(), "the overlay appearance must land on the baseline overlay rule");
        QVERIFY2(overlayRule.value(QStringLiteral("managed")).toBool(), "the overlay baseline must be managed");
        // Colours are carried even with UseSystem=true — the getter's live
        // palette gate decides application, not the migration.
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayHighlightColor")).toString(),
                 QStringLiteral("#ff112233"));
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayInactiveColor")).toString(),
                 QStringLiteral("#80445566"));
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayBorderColor")).toString(),
                 QStringLiteral("#ff778899"));
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayActiveOpacity")).toDouble(), 1.0);
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayInactiveOpacity")).toDouble(), 0.25);
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayBorderWidth")).toInt(), 10);
        QCOMPARE(actionValue(overlayRule, QStringLiteral("setOverlayBorderRadius")).toInt(), 12);

        // The folded keys are stripped; the UseSystem toggle survives.
        const QJsonObject zones = readJson(ConfigDefaults::configFilePath())
                                      .value(QStringLiteral("Snapping"))
                                      .toObject()
                                      .value(QStringLiteral("Zones"))
                                      .toObject();
        const QJsonObject colors = zones.value(QStringLiteral("Colors")).toObject();
        QCOMPARE(colors.value(QStringLiteral("UseSystem")).toBool(), true);
        QVERIFY2(!colors.contains(QStringLiteral("Highlight")), "highlight colour key must be stripped");
        QVERIFY2(!colors.contains(QStringLiteral("Inactive")), "inactive colour key must be stripped");
        QVERIFY2(!colors.contains(QStringLiteral("Border")), "border colour key must be stripped");
        const QJsonObject opacity = zones.value(QStringLiteral("Opacity")).toObject();
        QVERIFY2(!opacity.contains(QStringLiteral("Active")), "active opacity key must be stripped");
        QVERIFY2(!opacity.contains(QStringLiteral("Inactive")), "inactive opacity key must be stripped");
        const QJsonObject border = zones.value(QStringLiteral("Border")).toObject();
        QVERIFY2(!border.contains(QStringLiteral("Width")), "border width key must be stripped");
        QVERIFY2(!border.contains(QStringLiteral("Radius")), "border radius key must be stripped");
    }

    void testFinalizeV5_deferredWhenRulesFileCorrupt()
    {
        IsolatedConfigGuard guard;

        // Run the in-place v4→v5 transform on a config carrying a snapping
        // appearance override, producing the _v5AppearanceStash + version 5.
        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        ConfigMigration::migrateV4ToV5(cfg);
        QVERIFY2(cfg.contains(QStringLiteral("_v5AppearanceStash")),
                 "the transform must stash the differing appearance values");
        writeJson(ConfigDefaults::configFilePath(), cfg);

        // Corrupt rules.json so RuleSet::loadFromFile fails inside finalizeV5.
        {
            const QString rulesPath = ConfigDefaults::rulesFilePath();
            QDir().mkpath(QFileInfo(rulesPath).absolutePath());
            QFile rf(rulesPath);
            QVERIFY(rf.open(QIODevice::WriteOnly));
            rf.write(QByteArrayLiteral("{ this is not valid json"));
        }

        // The finalizer must DEFER: return false and preserve the stash.
        QVERIFY2(!ConfigMigration::finalizeV5Conversion(ConfigDefaults::configFilePath()),
                 "finalizeV5 must report failure when rules.json cannot be loaded");
        QVERIFY2(readJson(ConfigDefaults::configFilePath()).contains(QStringLiteral("_v5AppearanceStash")),
                 "the stash must be preserved for a later retry, not stripped");
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

        // One representative of EVERY v5 fold, so a re-run is proven a no-op
        // across all of them: per-mode appearance + gaps, per-screen tiling,
        // per-screen zone-selector, animation min-size, general min-size, and
        // the overlay baseline.
        QJsonObject cfg = baseV4Config();
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Appearance"), QStringLiteral("Borders")},
                  {{QStringLiteral("Width"), 5}});
        setNested(cfg, {QStringLiteral("Tiling"), QStringLiteral("Gaps")}, {{QStringLiteral("Inner"), 12}});
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("Autotile"), QStringLiteral("DP-1")},
                  {{QStringLiteral("AutotileSplitRatio"), 0.66}});
        setNested(cfg, {QStringLiteral("PerScreen"), QStringLiteral("ZoneSelector"), QStringLiteral("DP-1")},
                  {{QStringLiteral("Position"), 4}});
        setNested(cfg, {QStringLiteral("Animations"), QStringLiteral("WindowFiltering")},
                  {{QStringLiteral("MinimumWindowWidth"), 300}});
        setNested(cfg, {QStringLiteral("Exclusions")}, {{QStringLiteral("MinimumWindowWidth"), 300}});
        setNested(cfg, {QStringLiteral("Snapping"), QStringLiteral("Zones"), QStringLiteral("Border")},
                  {{QStringLiteral("Width"), 5}});
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        const int firstCount = rules().size();
        // 2 per-mode rules + tiling + zone-selector + animation min-size +
        // general min-size + overlay baseline = 7.
        QCOMPARE(firstCount, 7);

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

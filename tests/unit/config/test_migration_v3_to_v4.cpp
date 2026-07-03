// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4.cpp
 * @brief Unit tests for the v3 → v4 schema migration (window-rule
 *        consolidation).
 *
 * A v3 config.json + assignments.json fixture is run through
 * ConfigMigration::ensureJsonConfig; the test asserts:
 *   - rules.json is produced at `_version == 4`,
 *   - each migrated zone Assignment becomes a context rule seeded in the
 *     Context priority band (~301..306) by its pinned dimensions,
 *   - assignment rules carry SetEngineMode + (when non-empty)
 *     SetSnappingLayout / SetTilingAlgorithm,
 *   - the global default comes from a gated resolver, so no provider-default
 *     catch-all rule is emitted,
 *   - per-mode disable-list entries become DisableEngine context rules,
 *   - the v3 window-class/application exclusion lists fold into
 *     `AppId AppIdMatches` Exclude rules,
 *   - the v3 animation-application rules fold into animation-override rules
 *     and the animation exclusion lists fold into `ExcludeAnimations` rules,
 *   - the zone-overlay appearance groups are renamed from
 *     `Snapping.Appearance.*` to `Snapping.Zones.*` (key-for-key move,
 *     absent-source no-op, idempotent, coexisting with the folds above),
 *   - config.json is stamped at the current schema version (the chain runs
 *     past the v3→v4 step to ConfigSchemaVersion),
 *   - the conversion is idempotent (running twice is a no-op).
 *
 * rules.json SUPERSEDES the v3 inputs: the migration renames
 * assignments.json to assignments.json.migrated after rules.json is
 * durably written (a non-destructive retire that leaves the original data
 * recoverable from disk), removes the config.json Display.*Disabled* keys,
 * and relocates the QuickLayouts slots to the quicklayouts.json sidecar.
 * These superseding behaviours are asserted alongside the conversion
 * fidelity.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configkeys.h"
#include "../../../src/config/configmigration.h"
#include "../../../src/config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace CRB = PhosphorRules::ContextRuleBridge;

class TestMigrationV3ToV4 : public QObject
{
    Q_OBJECT

private:
    void writeJson(const QString& path, const QJsonObject& obj)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(obj).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
    }

    QJsonObject readJson(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

    /// The legacy assignments.json path. ConfigDefaults no longer exposes it
    /// (rules.json supersedes it in v4) — it sits beside rules.json
    /// in the same plasmazones config directory.
    static QString assignmentsPath()
    {
        return QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath() + QStringLiteral("/assignments.json");
    }

    /// A v3 config.json carrying per-mode disable lists + a global default
    /// snapping layout.
    QJsonObject makeV3Config()
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);

        QJsonObject display;
        display.insert(QStringLiteral("SnappingDisabledMonitors"), QStringLiteral("DP-3"));
        display.insert(QStringLiteral("AutotileDisabledMonitors"), QStringLiteral("DP-3,HDMI-2"));
        display.insert(QStringLiteral("SnappingDisabledDesktops"), QStringLiteral("DP-1/4"));
        display.insert(QStringLiteral("AutotileDisabledActivities"), QStringLiteral("DP-1/act-uuid-7"));
        root.insert(QStringLiteral("Display"), display);

        // Global default snapping layout — feeds the gated default resolver
        // (no provider-default rule is emitted).
        QJsonObject windowHandling;
        windowHandling.insert(QStringLiteral("DefaultLayoutId"),
                              QStringLiteral("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"));
        QJsonObject behavior;
        behavior.insert(QStringLiteral("WindowHandling"), windowHandling);
        QJsonObject snapping;
        snapping.insert(QStringLiteral("Behavior"), behavior);
        root.insert(QStringLiteral("Snapping"), snapping);

        return root;
    }

    /// A v3 assignments.json fixture exercising every cascade level.
    QJsonObject makeAssignments()
    {
        QJsonObject root;

        // Exact: screen + desktop + activity, autotile mode.
        QJsonObject exact;
        exact.insert(QStringLiteral("Mode"), 1); // Autotile
        exact.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-exact}"));
        exact.insert(QStringLiteral("TilingAlgorithm"), QStringLiteral("dwindle"));
        root.insert(QStringLiteral("Assignment:DP-2:Desktop:2:Activity:work-uuid"), exact);

        // Screen + activity, snapping mode.
        QJsonObject scrAct;
        scrAct.insert(QStringLiteral("Mode"), 0); // Snapping
        scrAct.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-act}"));
        scrAct.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2:Activity:play-uuid"), scrAct);

        // Screen + desktop, snapping mode.
        QJsonObject scrDesk;
        scrDesk.insert(QStringLiteral("Mode"), 0);
        scrDesk.insert(QStringLiteral("SnappingLayout"), QStringLiteral("{snap-desk}"));
        scrDesk.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2:Desktop:3"), scrDesk);

        // Screen only (display default), autotile mode-only (empty tiling algo).
        QJsonObject scrOnly;
        scrOnly.insert(QStringLiteral("Mode"), 1); // Autotile, default algorithm
        scrOnly.insert(QStringLiteral("SnappingLayout"), QString());
        scrOnly.insert(QStringLiteral("TilingAlgorithm"), QString());
        root.insert(QStringLiteral("Assignment:DP-2"), scrOnly);

        // QuickLayouts — NOT a rule.
        QJsonObject quick;
        quick.insert(QStringLiteral("3"), QStringLiteral("{quick-layout-id}"));
        root.insert(QStringLiteral("QuickLayouts"), quick);

        return root;
    }

    QJsonArray rulesFromRules()
    {
        const QJsonObject root = readJson(ConfigDefaults::rulesFilePath());
        return root.value(QStringLiteral("rules")).toArray();
    }

    QList<QJsonObject> allRulesByPriority(const QJsonArray& rules, int priority)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("priority")).toInt() == priority) {
                out.append(r);
            }
        }
        return out;
    }

    // An assignment rule sets an engine mode and does NOT disable an engine.
    // Disable rules are all seeded at the Context band base (300) while
    // assignment rules sit just above it (301..306), so they never share a
    // priority; this predicate still lets a priority-keyed lookup target the
    // assignment rule unambiguously.
    bool isAssignmentRule(const QJsonObject& rule)
    {
        const QStringList types = actionTypes(rule);
        return types.contains(QLatin1String("setEngineMode")) && !types.contains(QLatin1String("disableEngine"));
    }

    bool hasAssignmentAtPriority(const QJsonArray& rules, int priority)
    {
        for (const QJsonObject& r : allRulesByPriority(rules, priority)) {
            if (isAssignmentRule(r)) {
                return true;
            }
        }
        return false;
    }

    // Returns the assignment rule at @p priority, skipping any disable rule.
    // A bare first-match-by-priority lookup would return whichever rule the
    // migration happened to emit first — an order dependency the assertion
    // should not rely on.
    QJsonObject findAssignmentRuleByPriority(const QJsonArray& rules, int priority)
    {
        for (const QJsonObject& r : allRulesByPriority(rules, priority)) {
            if (isAssignmentRule(r)) {
                return r;
            }
        }
        return {};
    }

    /// Collect the action `type` strings of a rule.
    QStringList actionTypes(const QJsonObject& rule)
    {
        QStringList types;
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            types.append(v.toObject().value(QStringLiteral("type")).toString());
        }
        types.sort();
        return types;
    }

    /// Flatten a rule's match expression to its leaf objects. A bare leaf
    /// match yields a one-element list; an All{} match yields its children.
    QList<QJsonObject> matchLeaves(const QJsonObject& rule)
    {
        const QJsonObject match = rule.value(QStringLiteral("match")).toObject();
        QList<QJsonObject> leaves;
        if (match.contains(QStringLiteral("field"))) {
            leaves.append(match); // a bare equality leaf
        } else if (match.contains(QStringLiteral("all"))) {
            for (const QJsonValue& v : match.value(QStringLiteral("all")).toArray()) {
                leaves.append(v.toObject());
            }
        }
        return leaves;
    }

    /// The string value of the `field == equals` leaf for @p field, or empty
    /// if the rule's match carries no such leaf.
    QString matchLeafValue(const QJsonObject& rule, const QString& field)
    {
        return matchLeafValueByOp(rule, field, QStringLiteral("equals"));
    }

    /// Locate the first leaf with `field == @p field` AND `op == @p op`, and
    /// return its `value` as a string. Generalises @ref matchLeafValue (which
    /// is hard-wired to `equals` for the assignment / disable rules) to the
    /// animation-rule case where the matcher is `contains`.
    QString matchLeafValueByOp(const QJsonObject& rule, const QString& field, const QString& op)
    {
        for (const QJsonObject& leaf : matchLeaves(rule)) {
            if (leaf.value(QStringLiteral("field")).toString() == field
                && leaf.value(QStringLiteral("op")).toString() == op) {
                return leaf.value(QStringLiteral("value")).toVariant().toString();
            }
        }
        return QString();
    }

    /// The `mode` token of a rule's single `disableEngine` action, or empty
    /// if the rule carries no disable action.
    QString disableActionMode(const QJsonObject& rule)
    {
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("type")).toString() == QLatin1String("disableEngine")) {
                return a.value(QStringLiteral("mode")).toString();
            }
        }
        return QString();
    }

    /// All rules carrying a `disableEngine` action.
    QList<QJsonObject> disableRules(const QJsonArray& rules)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (!disableActionMode(r).isEmpty()) {
                out.append(r);
            }
        }
        return out;
    }

private Q_SLOTS:

    // ─── Full conversion ──────────────────────────────────────────────────

    void testFullConversion_producesRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // rules.json exists at _version 4.
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        const QJsonObject wr = readJson(ConfigDefaults::rulesFilePath());
        QCOMPARE(wr.value(QStringLiteral("_version")).toInt(), 4);

        // config.json stamped at the current schema version (migrateV3ToV4 also
        // seeds the decoration tree before stamping ConfigSchemaVersion).
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        // The migration chain now runs v3 → v4 → v5, so config.json lands at
        // the current schema version (the v3→v4 step still stamps 4 mid-chain).
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);

        // All four temporary stash keys are stripped from config.json.
        // The fixture's `makeV3Config()` doesn't populate the two
        // exclusion stashes, so they shouldn't exist post-migration
        // anyway — pinning their absence here catches a future
        // regression where the migration spuriously creates an empty
        // stash from absent input.
        QVERIFY(!cfg.contains(QStringLiteral("_v4DisableStash")));
        QVERIFY(!cfg.contains(QStringLiteral("_v4AnimationRulesStash")));
        QVERIFY(!cfg.contains(QStringLiteral("_v4ExclusionStash")));
        QVERIFY(!cfg.contains(QStringLiteral("_v4AnimationExclusionStash")));
    }

    void testLayoutAppRules_becomeSnapToZoneRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // A v3 layout file carrying two legacy app→zone rules: firefox → zone 2
        // (no screen), and konsole → zone 3 with a legacy targetScreen "DP-1"
        // (which v4 carries over as a RouteToScreen action — see below). They live
        // in the user data dir the migration scans.
        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        QJsonArray appRules;
        appRules.append(
            QJsonObject{{QStringLiteral("pattern"), QStringLiteral("firefox")}, {QStringLiteral("zoneNumber"), 2}});
        appRules.append(QJsonObject{{QStringLiteral("pattern"), QStringLiteral("konsole")},
                                    {QStringLiteral("zoneNumber"), 3},
                                    {QStringLiteral("targetScreen"), QStringLiteral("DP-1")}});
        writeJson(layoutsDir + QStringLiteral("/layout1.json"), QJsonObject{{QStringLiteral("appRules"), appRules}});

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromRules();

        // The 1-based zone ordinals carried by a rule's SnapToZone action.
        const auto snapZones = [](const QJsonObject& rule) -> QList<int> {
            QList<int> out;
            for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
                const QJsonObject a = v.toObject();
                if (a.value(QStringLiteral("type")).toString() == QLatin1String("snapToZone")) {
                    for (const QJsonValue& z : a.value(QStringLiteral("zones")).toArray()) {
                        out.append(z.toInt());
                    }
                }
            }
            return out;
        };

        QJsonObject firefoxRule;
        QJsonObject konsoleRule;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (!actionTypes(r).contains(QLatin1String("snapToZone"))) {
                continue;
            }
            const QString cls = matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches"));
            if (cls == QLatin1String("firefox")) {
                firefoxRule = r;
            } else if (cls == QLatin1String("konsole")) {
                konsoleRule = r;
            }
        }

        // The targetScreenId carried by a rule's RouteToScreen action (empty when
        // the rule has none).
        const auto routeScreen = [](const QJsonObject& rule) -> QString {
            for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
                const QJsonObject a = v.toObject();
                if (a.value(QStringLiteral("type")).toString() == QLatin1String("routeToScreen")) {
                    return a.value(QStringLiteral("targetScreenId")).toString();
                }
            }
            return QString();
        };

        // firefox → SnapToZone [2]; a single AppId-appIdMatches leaf (no screen).
        // No targetScreen in the source, so no RouteToScreen action is emitted.
        QVERIFY(!firefoxRule.isEmpty());
        QCOMPARE(snapZones(firefoxRule), (QList<int>{2}));
        QCOMPARE(matchLeaves(firefoxRule).size(), 1);
        QVERIFY(routeScreen(firefoxRule).isEmpty());
        QVERIFY(!actionTypes(firefoxRule).contains(QLatin1String("routeToScreen")));

        // konsole → SnapToZone [3] PLUS a RouteToScreen action carrying the legacy
        // targetScreen "DP-1". The MATCH stays a single AppId-appIdMatches leaf with
        // NO ScreenId constraint — RouteToScreen is an ACTION (it routes), not a
        // ScreenId match (which would only scope).
        QVERIFY(!konsoleRule.isEmpty());
        QCOMPARE(snapZones(konsoleRule), (QList<int>{3}));
        QCOMPARE(matchLeaves(konsoleRule).size(), 1);
        QVERIFY(matchLeafValueByOp(konsoleRule, QStringLiteral("screenId"), QStringLiteral("equals")).isEmpty());
        QCOMPARE(routeScreen(konsoleRule), QStringLiteral("DP-1"));
    }

    // The reporter's exact shape (discussion #686): a v3 appRule whose pattern is
    // the X11 two-token "resourceName resourceClass" form, pinned to a monitor.
    // v4 must (a) normalize the pattern to the single appId token the daemon keys
    // on — without this the AppIdMatches leaf would never fire — and (b) carry the
    // targetScreen as a RouteToScreen action so the app still opens on that monitor.
    void testLayoutAppRules_normalizesTwoTokenPatternAndRoutesScreen()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        const QString kGigabyte = QStringLiteral("GIGA-BYTE TECHNOLOGY CO., LTD.:MO34WQC:16843009");
        QJsonObject appRule;
        appRule.insert(QStringLiteral("pattern"), QStringLiteral("chromium chromium"));
        appRule.insert(QStringLiteral("zoneNumber"), 2);
        appRule.insert(QStringLiteral("targetScreen"), kGigabyte);
        QJsonObject layout;
        layout.insert(QStringLiteral("appRules"), QJsonArray{appRule});
        writeJson(layoutsDir + QStringLiteral("/columns.json"), layout);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Exactly one rule whose AppId leaf is the NORMALIZED single token.
        QJsonObject chromiumRule;
        for (const QJsonValue& v : rulesFromRules()) {
            const QJsonObject r = v.toObject();
            if (matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches"))
                == QLatin1String("chromium")) {
                chromiumRule = r;
            }
        }
        QVERIFY2(!chromiumRule.isEmpty(), "two-token 'chromium chromium' must normalize to the appId leaf 'chromium'");

        // The RouteToScreen action carries the legacy monitor verbatim.
        QString routedScreen;
        QList<int> zones;
        for (const QJsonValue& v : chromiumRule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            const QString type = a.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("routeToScreen")) {
                routedScreen = a.value(QStringLiteral("targetScreenId")).toString();
            } else if (type == QLatin1String("snapToZone")) {
                for (const QJsonValue& z : a.value(QStringLiteral("zones")).toArray()) {
                    zones.append(z.toInt());
                }
            }
        }
        QCOMPARE(zones, (QList<int>{2}));
        QCOMPARE(routedScreen, kGigabyte);
    }

    void testLayoutAppRules_dedupePatternAcrossLayouts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // Same pattern in two layout files mapping to DIFFERENT zones. A global
        // ordinal SnapToZone rule fires regardless of the active layout, so only
        // the first (name-order) wins; the second is dropped.
        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        writeJson(layoutsDir + QStringLiteral("/a-layout.json"),
                  QJsonObject{{QStringLiteral("appRules"),
                               QJsonArray{QJsonObject{{QStringLiteral("pattern"), QStringLiteral("mpv")},
                                                      {QStringLiteral("zoneNumber"), 1}}}}});
        writeJson(layoutsDir + QStringLiteral("/b-layout.json"),
                  QJsonObject{{QStringLiteral("appRules"),
                               QJsonArray{QJsonObject{{QStringLiteral("pattern"), QStringLiteral("mpv")},
                                                      {QStringLiteral("zoneNumber"), 4}}}}});

        QVERIFY(ConfigMigration::ensureJsonConfig());

        int mpvRuleCount = 0;
        QList<int> winningZones;
        for (const QJsonValue& v : rulesFromRules()) {
            const QJsonObject r = v.toObject();
            if (matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches"))
                != QLatin1String("mpv")) {
                continue;
            }
            ++mpvRuleCount;
            for (const QJsonValue& av : r.value(QStringLiteral("actions")).toArray()) {
                const QJsonObject a = av.toObject();
                if (a.value(QStringLiteral("type")).toString() == QLatin1String("snapToZone")) {
                    for (const QJsonValue& z : a.value(QStringLiteral("zones")).toArray()) {
                        winningZones.append(z.toInt());
                    }
                }
            }
        }
        QCOMPARE(mpvRuleCount, 1);
        QCOMPARE(winningZones, (QList<int>{1})); // a-layout.json wins on name order
    }

    void testLayoutAppRules_idempotentRuleIds()
    {
        // The SnapToZone migration's rule id is derived from
        // (normalized pattern, zoneNumber, targetScreen) via a fixed v5-UUID
        // namespace, so a crash-and-retry conversion yields byte-identical rules.
        // This mirrors the
        // sibling exclusion / animation folds' idempotency tests and pins the
        // namespace UUID + segment encoding so a future drift in either forces a
        // deliberate update here (the migration owns both ends of the derivation,
        // so a same-inputs→same-id check alone cannot catch a namespace change).
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        const QString layoutsDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1Char('/') + ConfigDefaults::layoutsSubdir();
        writeJson(layoutsDir + QStringLiteral("/layout1.json"),
                  QJsonObject{{QStringLiteral("appRules"),
                               QJsonArray{QJsonObject{{QStringLiteral("pattern"), QStringLiteral("firefox")},
                                                      {QStringLiteral("zoneNumber"), 2}}}}});

        // Finds the id of the SnapToZone rule whose AppId-appIdMatches leaf is the
        // given pattern.
        const auto snapRuleIdFor = [this](const QString& pattern) -> QString {
            for (const QJsonValue& v : rulesFromRules()) {
                const QJsonObject r = v.toObject();
                if (actionTypes(r).contains(QLatin1String("snapToZone"))
                    && matchLeafValueByOp(r, QStringLiteral("appId"), QStringLiteral("appIdMatches")) == pattern) {
                    return r.value(QStringLiteral("id")).toString();
                }
            }
            return {};
        };

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId = snapRuleIdFor(QStringLiteral("firefox"));
        QVERIFY(!firstId.isEmpty());

        // Golden assertion against the SPEC: namespace UUID + length-prefixed
        // segment encoding ("<size>:<bytes>" per segment, no separator). The id is
        // derived from (normalized pattern, zoneNumber, targetScreen). This fixture
        // pins firefox with no targetScreen, so the third segment is the empty
        // string ("0:").
        //   segment 1 → pattern      "firefox" → "7:firefox"
        //   segment 2 → zoneNumber   "2"       → "1:2"
        //   segment 3 → targetScreen ""        → "0:"
        const QUuid kExpectedNamespace(QStringLiteral("{6f1c8e44-2a7b-5d93-8e10-4b2c9a7f1d35}"));
        const QString kExpectedKey = QStringLiteral("7:firefox") + QStringLiteral("1:2") + QStringLiteral("0:");
        QCOMPARE(firstId, QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString());

        // Force the rebuild path again and re-stage the same v3 inputs.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(layoutsDir + QStringLiteral("/layout1.json"),
                  QJsonObject{{QStringLiteral("appRules"),
                               QJsonArray{QJsonObject{{QStringLiteral("pattern"), QStringLiteral("firefox")},
                                                      {QStringLiteral("zoneNumber"), 2}}}}});
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(snapRuleIdFor(QStringLiteral("firefox")), firstId);
    }

    // ─── Exact cascade priorities ─────────────────────────────────────────

    void testCascadePriorities_exactValues()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromRules();

        // Each fixture Assignment migrated to a rule seeded in the Context
        // priority band by its pinned dimensions
        // (300 + activity?3 + desktop?2 + screen?1). The four pinned levels
        // must all be present in the migrated rule set.
        //
        // Disable rules all sit at the band base (300), so they never share a
        // priority with these assignment rules; the assignment-filtered helper
        // (a rule that sets an engine mode and does NOT disable an engine) keeps
        // the lookup unambiguous regardless.

        // Exact (screen+desktop+activity) → 306.
        QVERIFY(hasAssignmentAtPriority(rules, 306));
        // Screen + activity → 304 (activity nudge beats desktop).
        QVERIFY(hasAssignmentAtPriority(rules, 304));
        // Screen + desktop → 303.
        QVERIFY(hasAssignmentAtPriority(rules, 303));
        // Screen only → 301.
        QVERIFY(hasAssignmentAtPriority(rules, 301));
    }

    // ─── Lossless three-action assignment rules ──────────────────────────

    void testAssignmentRule_carriesAllThreeActions()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromRules();

        // The exact rule (306) had Mode=Autotile + snappingLayout + tilingAlgo
        // — all three actions present. Disable rules all sit at the band base
        // (300), so none collides with an assignment rule; the
        // assignment-filtered lookup is used uniformly so the three sites stay
        // consistent.
        const QJsonObject exact = findAssignmentRuleByPriority(rules, 306);
        QVERIFY(!exact.isEmpty());
        QCOMPARE(actionTypes(exact),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout"),
                              QStringLiteral("setTilingAlgorithm")}));

        // The screen+activity rule (304) had snapping mode + a layout, no
        // tiling algorithm → SetEngineMode + SetSnappingLayout only.
        const QJsonObject scrAct = findAssignmentRuleByPriority(rules, 304);
        QVERIFY(!scrAct.isEmpty());
        QCOMPARE(actionTypes(scrAct),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout")}));

        // The screen-only rule (301) was mode-only autotile (both layout
        // fields empty) → just SetEngineMode.
        const QJsonObject scrOnly = findAssignmentRuleByPriority(rules, 301);
        QVERIFY(!scrOnly.isEmpty());
        QCOMPARE(actionTypes(scrOnly), (QStringList{QStringLiteral("setEngineMode")}));
    }

    // ─── Disable-list rules ───────────────────────────────────────────────

    void testDisableListRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> disabled = disableRules(rulesFromRules());

        // Count DisableEngine rules. Fixture: snapping monitor (DP-3) = 1,
        // autotile monitors (DP-3, HDMI-2) = 2, snapping desktop (DP-1/4) = 1,
        // autotile activity (DP-1/act-uuid-7) = 1 → 5 disable rules.
        QCOMPARE(disabled.size(), 5);

        // Count alone is not enough — a migration that swapped screen ids or
        // modes still hits 5. Assert each fixture entry produced a disable
        // rule with the correct pinned dimensions AND the correct mode token.

        // SnappingDisabledMonitors = "DP-3" → snapping monitor disable on DP-3.
        const auto isSnapMonitorDp3 = [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("screenId")) == QLatin1String("DP-3")
                && matchLeafValue(r, QStringLiteral("virtualDesktop")).isEmpty()
                && matchLeafValue(r, QStringLiteral("activity")).isEmpty();
        };
        QCOMPARE(std::count_if(disabled.cbegin(), disabled.cend(), isSnapMonitorDp3), 1);

        // AutotileDisabledMonitors = "DP-3,HDMI-2" → autotile monitor disables
        // on BOTH DP-3 and HDMI-2.
        const auto isAutotileMonitor = [&](const QJsonObject& r, const QString& screen) {
            return disableActionMode(r) == QLatin1String("autotile")
                && matchLeafValue(r, QStringLiteral("screenId")) == screen
                && matchLeafValue(r, QStringLiteral("virtualDesktop")).isEmpty()
                && matchLeafValue(r, QStringLiteral("activity")).isEmpty();
        };
        QCOMPARE(std::count_if(disabled.cbegin(), disabled.cend(),
                               [&](const QJsonObject& r) {
                                   return isAutotileMonitor(r, QStringLiteral("DP-3"));
                               }),
                 1);
        QCOMPARE(std::count_if(disabled.cbegin(), disabled.cend(),
                               [&](const QJsonObject& r) {
                                   return isAutotileMonitor(r, QStringLiteral("HDMI-2"));
                               }),
                 1);

        // SnappingDisabledDesktops = "DP-1/4" → snapping desktop disable
        // pinning ScreenId == DP-1 AND VirtualDesktop == 4.
        const auto isSnapDesktop = [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("screenId")) == QLatin1String("DP-1")
                && matchLeafValue(r, QStringLiteral("virtualDesktop")) == QLatin1String("4")
                && matchLeafValue(r, QStringLiteral("activity")).isEmpty();
        };
        QCOMPARE(std::count_if(disabled.cbegin(), disabled.cend(), isSnapDesktop), 1);

        // AutotileDisabledActivities = "DP-1/act-uuid-7" → autotile activity
        // disable pinning ScreenId == DP-1 AND Activity == act-uuid-7.
        const auto isAutotileActivity = [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("autotile")
                && matchLeafValue(r, QStringLiteral("screenId")) == QLatin1String("DP-1")
                && matchLeafValue(r, QStringLiteral("activity")) == QLatin1String("act-uuid-7")
                && matchLeafValue(r, QStringLiteral("virtualDesktop")).isEmpty();
        };
        QCOMPARE(std::count_if(disabled.cbegin(), disabled.cend(), isAutotileActivity), 1);
    }

    // ─── Disable-rule priority: seeded in the Context band ────────────────
    // Disable rules no longer follow a multi-dimension cascade: every migrated
    // DisableEngine rule is seeded at the Context band base
    // (kContextBandBase = 300) regardless of how many dimensions it pins. This
    // asserts the screen+desktop, screen+activity, and screen-only disables all
    // land at the same band value, and that makeDisableRule agrees.

    void testDisableRulePriority_seededInContextBand()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> disabled = disableRules(rulesFromRules());

        // The "DP-1/4" SnappingDisabledDesktops entry pins screen + desktop —
        // seeded at the Context band base (300), no per-dimension nudge.
        const auto snapDesktop = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("virtualDesktop")) == QLatin1String("4");
        });
        QVERIFY(snapDesktop != disabled.cend());
        QCOMPARE(snapDesktop->value(QStringLiteral("priority")).toInt(),
                 PhosphorRules::ContextRuleBridge::kContextBandBase);

        // The "DP-1/act-uuid-7" AutotileDisabledActivities entry pins screen +
        // activity — also seeded at the band base (300).
        const auto autotileActivity = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("autotile")
                && matchLeafValue(r, QStringLiteral("activity")) == QLatin1String("act-uuid-7");
        });
        QVERIFY(autotileActivity != disabled.cend());
        QCOMPARE(autotileActivity->value(QStringLiteral("priority")).toInt(),
                 PhosphorRules::ContextRuleBridge::kContextBandBase);

        // A screen-only monitor disable sits at the band base (300) too.
        const auto snapMonitor = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("screenId")) == QLatin1String("DP-3");
        });
        QVERIFY(snapMonitor != disabled.cend());
        QCOMPARE(snapMonitor->value(QStringLiteral("priority")).toInt(),
                 PhosphorRules::ContextRuleBridge::kContextBandBase);

        // makeDisableRule must agree with the migration output: every disable
        // rule is seeded at the band base regardless of the pinned dimensions.
        const PhosphorRules::Rule directDesktop =
            CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), /*virtualDesktop=*/4, QString(),
                                 QStringLiteral("snapping"), PhosphorRules::ContextRuleBridge::kContextBandBase);
        QCOMPARE(directDesktop.priority, PhosphorRules::ContextRuleBridge::kContextBandBase);
        const PhosphorRules::Rule directActivity =
            CRB::makeDisableRule(QStringLiteral("a"), QStringLiteral("DP-1"), 0, QStringLiteral("act-uuid-7"),
                                 QStringLiteral("autotile"), PhosphorRules::ContextRuleBridge::kContextBandBase);
        QCOMPARE(directActivity.priority, PhosphorRules::ContextRuleBridge::kContextBandBase);
    }

    // ─── Idempotency ──────────────────────────────────────────────────────

    void testIdempotency_runTwiceIsNoOp()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!firstRun.isEmpty());

        // The process-level migration guard would normally short-circuit;
        // reset it so the second call re-runs the full logic against the
        // same (now-v4) config — which must be a clean no-op.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        // The `rulesAlreadyConverted` probe loads rules.json as a
        // v4 RuleSet; on the second run it succeeds, so finalize takes
        // the already-converted branch and only retries the idempotent
        // cleanup steps instead of rebuilding — rules.json is
        // byte-identical, the rule count is unchanged.
        QCOMPARE(secondRun, firstRun);
    }

    // ─── No-assignments fixture ───────────────────────────────────────────

    void testNoAssignments_writesNoProviderDefault()
    {
        IsolatedConfigGuard guard;
        // A v3 config with no assignments.json at all.
        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 3);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        const QJsonArray rules = rulesFromRules();
        // Exactly one rule: the premade Steam exclusion rule (seeded
        // unconditionally on every fresh/migrated v4 config). No provider-default
        // catch-all rule is emitted — the global default now comes from the gated
        // resolver, not a rule.
        QCOMPARE(rules.size(), 1);

        // No empty-All{} catch-all assignment rule (one carrying a setEngineMode
        // action) may be present.
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            const QJsonObject m = r.value(QStringLiteral("match")).toObject();
            const bool emptyAll =
                m.contains(QStringLiteral("all")) && m.value(QStringLiteral("all")).toArray().isEmpty();
            QVERIFY2(!(emptyAll && actionTypes(r).contains(QLatin1String("setEngineMode"))),
                     "no empty-All{} provider-default rule may be emitted");
        }
    }

    // ─── Premade Steam rule ───────────────────────────────────────────────
    // Every fresh install and every v3→v4 upgrade is seeded with the built-in
    // Steam tiling fix: exclude every `steam`-class window whose title is NOT
    // exactly "Steam" (Friends List, notification toasts, settings, chat),
    // leaving the main library window tileable.

    void testSteamDefaultRule_seeded()
    {
        IsolatedConfigGuard guard;
        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 3);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromRules();
        QJsonObject steam;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("name")).toString() == QLatin1String("Steam")) {
                steam = r;
            }
        }
        QVERIFY2(!steam.isEmpty(), "premade Steam rule must be seeded on a fresh/migrated v4 config");
        QVERIFY(steam.value(QStringLiteral("enabled")).toBool());

        // Composite match (the None{} guard below makes it non-simple), so
        // assignBandPrioritiesToZeroRules seeds it in the Advanced band [500,600)
        // rather than the Application band the simple AppId excludes get.
        const int steamPriority = steam.value(QStringLiteral("priority")).toInt();
        QVERIFY(steamPriority >= 500 && steamPriority < 600);

        // A single terminal Exclude action — the window is left unmanaged by
        // snap/tile.
        QCOMPARE(actionTypes(steam), (QStringList{QStringLiteral("exclude")}));

        // Match shape: All{ WindowClass contains "steam", None{ Title equals "Steam" } }.
        const QJsonObject match = steam.value(QStringLiteral("match")).toObject();
        QVERIFY(match.contains(QStringLiteral("all")));
        const QJsonArray all = match.value(QStringLiteral("all")).toArray();
        QCOMPARE(all.size(), 2);

        // WindowClass contains "steam" (matches KWin's raw "resourceName
        // resourceClass" string case-insensitively).
        QCOMPARE(matchLeafValueByOp(steam, QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("steam"));

        // None{ Title equals "Steam" } — the negative guard that keeps the
        // main library window (title exactly "Steam") tileable.
        bool foundTitleGuard = false;
        for (const QJsonValue& v : all) {
            const QJsonObject child = v.toObject();
            if (!child.contains(QStringLiteral("none"))) {
                continue;
            }
            const QJsonArray none = child.value(QStringLiteral("none")).toArray();
            QCOMPARE(none.size(), 1);
            const QJsonObject leaf = none.first().toObject();
            QCOMPARE(leaf.value(QStringLiteral("field")).toString(), QStringLiteral("title"));
            QCOMPARE(leaf.value(QStringLiteral("op")).toString(), QStringLiteral("equals"));
            QCOMPARE(leaf.value(QStringLiteral("value")).toVariant().toString(), QStringLiteral("Steam"));
            foundTitleGuard = true;
        }
        QVERIFY2(foundTitleGuard, "Steam rule must carry a None{ Title equals \"Steam\" } guard");

        // The rule is sliced into the Exclude rule set the daemon/effect
        // consume — i.e. it actually participates in the exclusion gate.
        const auto set = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY(set.has_value());
        QCOMPARE(PhosphorRules::ExclusionRules::excludeRulesFrom(*set).count(), 1);
    }

    // ─── Superseding: assignments.json retired to .migrated ───────────────

    /// rules.json supersedes assignments.json — once the rule store is
    /// durably written, the legacy file is renamed to assignments.json.migrated
    /// (the irreversible commit). Rename is preferred over deletion so a
    /// downgrade or manual recovery can restore the previous schema.
    void testSupersede_assignmentsJsonRetired()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(QFile::exists(assignmentsPath()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // rules.json written; assignments.json retired from its original
        // location (renamed to .migrated, or in the fallback path removed).
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        QVERIFY2(!QFile::exists(assignmentsPath()), "assignments.json must be retired once rules.json supersedes it");
    }

    // ─── Superseding: Display.*Disabled* keys removed ─────────────────────

    /// migrateV3ToV4 removes the six config.json Display.*Disabled* keys for
    /// real — rules.json now carries them as DisableEngine rules, so a
    /// stale duplicate in config.json would be a split source of truth.
    void testSupersede_displayDisabledKeysRemoved()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        const QJsonObject display = cfg.value(QStringLiteral("Display")).toObject();
        // The four disable keys the v3 fixture set must all be gone.
        QVERIFY2(!display.contains(QStringLiteral("SnappingDisabledMonitors")),
                 "SnappingDisabledMonitors must be removed by the v4 migration");
        QVERIFY(!display.contains(QStringLiteral("AutotileDisabledMonitors")));
        QVERIFY(!display.contains(QStringLiteral("SnappingDisabledDesktops")));
        QVERIFY(!display.contains(QStringLiteral("AutotileDisabledActivities")));
        // The migration drops the Display group entirely once it is empty.
        QVERIFY(!cfg.contains(QStringLiteral("Display")));
    }

    // ─── Superseding: QuickLayouts relocated to sidecar ───────────────────

    /// QuickLayouts slots are not rules — the migration relocates them
    /// to the quicklayouts.json sidecar (the file LayoutRegistry reads). The v3
    /// slots are snapping bindings, written under the snapping key of the single
    /// mode-nested format (no flat variant).
    void testSupersede_quickLayoutsRelocatedToSidecar()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString sidecar = ConfigDefaults::quickLayoutsFilePath();
        QVERIFY2(QFile::exists(sidecar), "QuickLayouts must be relocated to quicklayouts.json");
        const QJsonObject slots = readJson(sidecar);
        // Mode-nested format: v3 slots land under "snapping"; "autotile" is present and empty.
        const QJsonObject snapping = slots.value(QStringLiteral("snapping")).toObject();
        QCOMPARE(snapping.value(QStringLiteral("3")).toString(), QStringLiteral("{quick-layout-id}"));
        QVERIFY2(slots.contains(QStringLiteral("autotile")), "the nested format always carries both mode keys");
        QVERIFY2(!slots.contains(QStringLiteral("3")), "slots must be nested by mode, not written flat");

        // The QuickLayouts data must not have leaked into rules.json as
        // a rule — it is not a rule.
        const QJsonArray rules = rulesFromRules();
        for (const QJsonValue& v : rules) {
            QVERIFY(!actionTypes(v.toObject()).contains(QStringLiteral("quickLayout")));
        }
    }

    // ─── Idempotency of the superseding behaviour ─────────────────────────

    /// Running the migration a second time after assignments.json is already
    /// retired is a clean no-op: the idempotency guard short-circuits on the
    /// existing v4 rules.json, nothing is re-created or re-retired.
    void testSupersede_idempotentAfterAssignmentsRetired()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(!QFile::exists(assignmentsPath()));
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!firstRun.isEmpty());

        // Second run against the already-converted, assignments-less tree.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // assignments.json is not re-created; rules.json is byte-identical.
        QVERIFY(!QFile::exists(assignmentsPath()));
        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(secondRun, firstRun);
    }

    // ─── Data-loss regression: delete-failure must not clobber the store ──
    //
    // Simulates the scenario where the assignments.json retire step fails (a
    // read-only filesystem, a lock, a permissions error) so the legacy file is
    // still present on the next startup. The conversion is already complete —
    // rules.json exists as a valid v4 RuleSet, and the user has
    // since authored an extra rule via the rule editor. Re-running
    // finalizeV4Conversion MUST NOT rebuild-and-overwrite rules.json from
    // the dead assignments.json: the user's rule must survive.
    //
    // The fix gates the rebuild on `!rulesAlreadyConverted` (probed by
    // actually loading rules.json as a RuleSet), NOT on
    // assignments.json's absence — so a permanently-undeletable assignments.json
    // can no longer clobber the rule store on every launch.
    void testDeleteFailure_doesNotOverwriteUserRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // First run: full conversion produces rules.json.
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));

        // The user authors a new rule via the rule editor — load the store,
        // append a rule, persist it. This rule exists ONLY in rules.json;
        // it has no counterpart in assignments.json.
        auto setWithUserRule = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY2(setWithUserRule.has_value(), "rules.json must parse as a v4 rule set");
        const PhosphorRules::Rule userRule =
            CRB::makeDisableRule(QStringLiteral("User-authored · DP-9"), QStringLiteral("DP-9"),
                                 /*virtualDesktop=*/0, QString(), QStringLiteral("snapping"),
                                 PhosphorRules::ContextRuleBridge::kContextBandBase);
        const QUuid userRuleId = userRule.id;
        QVERIFY(setWithUserRule->addRule(userRule));
        QVERIFY(setWithUserRule->saveToFile(ConfigDefaults::rulesFilePath()));
        const int countWithUserRule = setWithUserRule->count();
        QVERIFY(countWithUserRule > 0);

        // Simulate the retire-step failure: assignments.json is still present
        // on disk (as if QFile::remove / rename had failed on the first run).
        // Without the fix, the old idempotency guard — gated on
        // assignments.json's absence — would NOT short-circuit, so the rebuild
        // path would re-run and overwrite rules.json, destroying the
        // user's rule.
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(QFile::exists(assignmentsPath()));

        // Re-run the migration against the already-converted tree.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The user's rule MUST survive — rules.json was not rebuilt.
        auto afterRerun = PhosphorRules::RuleSet::loadFromFile(ConfigDefaults::rulesFilePath());
        QVERIFY2(afterRerun.has_value(), "rules.json must still parse as a v4 rule set after the re-run");
        QVERIFY2(afterRerun->ruleById(userRuleId).has_value(),
                 "the user-authored rule must survive a re-run with assignments.json still present");
        QCOMPARE(afterRerun->count(), countWithUserRule);

        // The cleanup-only branch still retires the leftover assignments.json
        // (quarantined to .migrated, or deleted) so it cannot loop forever.
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "the cleanup-only branch must retire the leftover assignments.json");
    }

    // ─── Data-loss regression: malformed rules.json aborts ─────────
    //
    // Sibling of testMalformedAssignmentsJsonAborts for the rebuild path's
    // rules.json prevalidate. When rules.json exists but doesn't
    // parse, the "already converted" probe (loadFromFile().has_value()) drops
    // into the rebuild path, which would otherwise overwrite the corrupt-but-
    // recoverable original with a stub seed-only rule set — destroying
    // every user-authored rule. The new prevalidateRulesFile fires
    // FIRST: quarantines to .corrupt.bak, refuses to commit, returns false.
    void testMalformedRulesJsonAborts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        // No legacy assignments file — this isolates the rules-only
        // corruption path. A fresh install with a corrupt rules.json
        // is the cleanest reproduction.
        const QString corruptPath = ConfigDefaults::rulesFilePath();
        QDir().mkpath(QFileInfo(corruptPath).absolutePath());
        const QByteArray corruptBytes = QByteArrayLiteral("{ \"_version\": 4, \"rules\": [");
        {
            QFile f(corruptPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(corruptBytes), static_cast<qint64>(corruptBytes.size()));
        }

        QVERIFY2(!ConfigMigration::ensureJsonConfig(), "ensureJsonConfig must return false on a malformed rules.json");

        // The corrupt file was quarantined to .corrupt.bak with bytes preserved.
        const QString corruptBak = corruptPath + QStringLiteral(".corrupt.bak");
        QVERIFY2(QFile::exists(corruptBak), "the malformed rules.json must be quarantined to .corrupt.bak");
        {
            QFile f(corruptBak);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), corruptBytes);
        }

        // Original is gone (renamed). A new stub rules.json must NOT
        // have been written — that's the data-loss class the guard exists for.
        QVERIFY(!QFile::exists(corruptPath));

        // config.json's chain step (migrateV3ToV4) DID run before finalize —
        // it stamps `_version=4` and stashes any disable-list / animation-rule
        // data. The chain step's idempotency guard then short-circuits the
        // next attempt; the rebuild branch at finalize takes over (rules.json
        // doesn't exist after quarantine, so the "already converted" probe
        // returns false and rebuild retries from the stash). Both paths
        // surface as a follow-up run after the user repairs the quarantine.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        // The migration chain now runs v3 → v4 → v5, so config.json lands at
        // the current schema version (the v3→v4 step still stamps 4 mid-chain).
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
    }

    // ─── Data-loss regression (B5): malformed assignments.json aborts ─────
    //
    // A corrupt assignments.json (truncation, power-loss, hand-edit error)
    // must NOT silently produce a rules.json holding only the disable + seed
    // rules — that would lose every pinned assignment AND
    // the quick-layout slots. The migration aborts loudly: the corrupt file
    // is quarantined to `.corrupt.bak` (NOT `.migrated`, which would imply a
    // successful migration), rules.json is NOT written, and config.json
    // keeps its v3 stamp so the next run can re-attempt after the user
    // repairs the sidecar.
    void testMalformedAssignmentsJsonAborts()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());

        // Truncated / hand-edited corruption: a non-empty payload that fails
        // to parse as JSON. Whitespace-only is intentionally NOT what we
        // simulate — that case is treated as a fresh install (no assignments
        // to migrate), not corruption.
        const QString corruptPath = assignmentsPath();
        QDir().mkpath(QFileInfo(corruptPath).absolutePath());
        const QByteArray corruptBytes = QByteArrayLiteral("{ \"Assignment:DP-2\": { \"Mode\": 1, ");
        {
            QFile f(corruptPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(corruptBytes), static_cast<qint64>(corruptBytes.size()));
        }

        // Migration MUST abort.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false on a malformed assignments.json");

        // rules.json must NOT have been created — silently writing a
        // disable-only rule set would mask the data-loss.
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must not be written when the legacy sidecar is corrupt");

        // The corrupt file was quarantined to .corrupt.bak with its original
        // bytes preserved — the user can inspect and repair it.
        const QString corruptBak = corruptPath + QStringLiteral(".corrupt.bak");
        QVERIFY2(QFile::exists(corruptBak), "the malformed assignments.json must be quarantined to .corrupt.bak");
        {
            QFile f(corruptBak);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), corruptBytes);
        }

        // NOT `.migrated`: that suffix implies a successful migration and
        // would mask the failure on next inspection.
        QVERIFY2(!QFile::exists(corruptPath + QStringLiteral(".migrated")),
                 "the corrupt file must NOT be quarantined as .migrated");

        // The original assignments.json no longer exists at its primary path
        // (it was renamed to .corrupt.bak).
        QVERIFY(!QFile::exists(corruptPath));

        // config.json keeps its v3 stamp — the on-disk schema version was
        // NOT bumped, so the next run will re-attempt the migration. The
        // user's path forward: repair `.corrupt.bak`, rename it back to
        // assignments.json, and re-run.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), 3);
    }

    // ─── Data-loss regression: stalled chain refuses to commit ──────────
    //
    // Stalled-chain gate: when config.json is stamped at a pre-v4 version
    // (chain stalled — e.g. migrateV1ToV2's side-effect writes failed and
    // MigrationRunner::runOnFile returned true for a no-op chain),
    // finalizeV4Conversion must refuse to commit a stub rules.json.
    // Otherwise the next successful run would hit `rulesAlreadyConverted
    // = true` in the cleanup branch and strip `_v4DisableStash` /
    // `_v4AnimationRulesStash` without porting them into rules — silently
    // losing the user's disable lists and animation app rules forever.
    void testFinalizeV4Conversion_refusesToCommitWhenChainStalled()
    {
        IsolatedConfigGuard guard;

        // Construct a v3-stamped config.json carrying both v4 scratch keys —
        // the shape a partially-advanced chain would produce after migrateV3ToV4
        // ran but the chain failed to bump version to 4 (synthetic, but exactly
        // the on-disk shape the gate must catch).
        QJsonObject cfg;
        cfg.insert(ConfigKeys::versionKey(), 3);
        QJsonObject disableStash;
        // Use the real v4 stash wire shape: production moveDisableKey calls
        // `display.value(configKey).toString()` and stashes that string verbatim
        // (CSV), not an array. A downstream test that asserts "stash content
        // was consumed into rules" would silently get a false positive if we
        // used an array here — toString() returns "" for a JSON array, and
        // appendMonitorRules's empty-input early-return would no-op.
        disableStash.insert(QStringLiteral("snappingMonitors"), QStringLiteral("DP-1"));
        disableStash.insert(QStringLiteral("autotileMonitors"), QStringLiteral("DP-1"));
        cfg.insert(QStringLiteral("_v4DisableStash"), disableStash);
        QJsonArray animStash;
        QJsonObject animEntry;
        animEntry.insert(QStringLiteral("classPattern"), QStringLiteral("firefox"));
        animEntry.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        animEntry.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        animEntry.insert(QStringLiteral("effectId"), QStringLiteral("dissolve"));
        animStash.append(animEntry);
        cfg.insert(QStringLiteral("_v4AnimationRulesStash"), animStash);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        // Call finalizeV4Conversion DIRECTLY so the chain doesn't get a
        // chance to advance _version to 4 before finalize runs. This is
        // the only way to exercise the chain-stalled gate at
        // configmigration.cpp's `if (configVersion < ConfigSchemaVersion)`
        // branch — ensureJsonConfig() runs the chain first, which on a
        // clean fixture lands at v4 and routes finalize through the
        // already-converted path.
        const bool ok = ConfigMigration::finalizeV4Conversion(ConfigDefaults::configFilePath());
        QVERIFY2(!ok, "finalizeV4Conversion must return false when _version < ConfigSchemaVersion");
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must NOT be committed when config.json is still below v4");
        const QJsonObject onDisk = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(onDisk.value(ConfigKeys::versionKey()).toInt(0) == 3,
                 "the v3 stamp must survive — finalize is not allowed to advance the version");
        QVERIFY2(onDisk.contains(QStringLiteral("_v4DisableStash")),
                 "stash keys must survive on disk so the next run can retry");
        QVERIFY2(onDisk.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "animation stash must survive on disk so the next run can retry");
    }

    // ─── Data-loss regression (B4): QuickLayouts write failure is recoverable ─
    //
    // The v3→v4 conversion writes two files: quicklayouts.json (sidecar) and
    // rules.json (the irreversible commit marker). Writing the sidecar
    // FIRST means a sidecar failure aborts BEFORE committing rules.json
    // — the user's slots stay recoverable from assignments.json on the next
    // attempt. Writing rules.json first would gate the rebuild path off
    // forever on the next run (the cleanup-only branch never re-attempts the
    // QuickLayouts relocation), losing the slots to the .migrated quarantine.
    //
    // We simulate the sidecar write failure by pre-creating a DIRECTORY at
    // the quicklayouts.json path: QSaveFile cannot replace a directory with a
    // file, so the write fails deterministically.
    void testQuickLayoutsWriteFailureRecoverable()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // Wedge the sidecar write: a non-empty directory at the target path
        // makes the atomic-write step fail. QSaveFile's commit() renames a
        // temp file onto `quicklayouts.json`; renaming a file onto a non-empty
        // directory fails deterministically on POSIX (ENOTEMPTY/EISDIR), and
        // the directory stays failed for the duration of the first attempt.
        const QString quickLayoutsPath = ConfigDefaults::quickLayoutsFilePath();
        QDir().mkpath(QFileInfo(quickLayoutsPath).absolutePath());
        QVERIFY(QDir().mkpath(quickLayoutsPath));
        QVERIFY(QFileInfo(quickLayoutsPath).isDir());
        // Pin the wedge: a child file makes the directory non-empty so
        // rename() can't succeed on any filesystem.
        {
            QFile pin(quickLayoutsPath + QStringLiteral("/.pin"));
            QVERIFY(pin.open(QIODevice::WriteOnly));
            QCOMPARE(pin.write("x"), static_cast<qint64>(1));
        }

        // First attempt: the sidecar write fails, migration aborts BEFORE
        // committing rules.json. The legacy sidecar must still be on
        // disk (we never reach the retire step), so the data is recoverable.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false when the QuickLayouts sidecar write fails");
        QVERIFY2(!QFile::exists(ConfigDefaults::rulesFilePath()),
                 "rules.json must NOT be written when the sidecar write fails");
        QVERIFY2(QFile::exists(assignmentsPath()),
                 "assignments.json must remain on disk so the user can re-attempt the migration");

        // Recovery: remove the wedge (delete the pin file, then the
        // directory) so the next run can write the sidecar. The user's
        // environment is otherwise unchanged.
        QVERIFY(QFile::remove(quickLayoutsPath + QStringLiteral("/.pin")));
        QVERIFY(QDir().rmdir(quickLayoutsPath));
        QVERIFY(!QFileInfo::exists(quickLayoutsPath));

        // Second attempt: full migration succeeds. rules.json is
        // written, the sidecar is populated, and the legacy file is retired.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY2(ConfigMigration::ensureJsonConfig(),
                 "the migration must succeed once the QuickLayouts sidecar write can complete");
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        QVERIFY2(QFile::exists(quickLayoutsPath), "the QuickLayouts sidecar must be populated on the second attempt");
        const QJsonObject slots = readJson(quickLayoutsPath);
        QCOMPARE(slots.value(QStringLiteral("snapping")).toObject().value(QStringLiteral("3")).toString(),
                 QStringLiteral("{quick-layout-id}"));
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "assignments.json must be retired once the full conversion completes");
    }

    // ─── Animation App Rules → Rules ────────────────────────────────
    //
    // The v3 `Animations.AnimationAppRules` array folds into rules.json
    // as `OverrideAnimation{Shader,Timing}` actions on a
    // `WindowClass Contains <pattern>` matcher. Below covers the conversion
    // shape, the drop-on-malformed contract, the engaged-blocking / inherit
    // sentinels, and idempotent rule-id derivation.

private:
    /// Build a v3 config carrying ONLY animation app rules (no Display.* keys,
    /// no Snapping defaults). Keeps animation-specific tests narrow — they
    /// don't have to filter the disable-rule + seed-rule noise out.
    ///
    /// The v4 group / key names are pinned as inline `QStringLiteral`
    /// literals — the test's role is to be an INDEPENDENT WITNESS of the
    /// frozen v4 on-disk wire format. If a future maintainer violates the
    /// freeze policy (forbidden by the `Legacy` struct's freeze-policy
    /// comment block in configkeys.h) and renames
    /// `Legacy::v4AnimationAppRulesKey` from "AnimationAppRules", the
    /// accessor-based form would silently update in lockstep with
    /// production; inline-literal pins catch the drift. Mirrors the
    /// sibling `makeV3Config` fixture's pattern for Display.* v3 keys.
    QJsonObject makeV3ConfigWithAnimationRules(const QJsonArray& animationRules)
    {
        QJsonObject root;
        root.insert(ConfigKeys::versionKey(), 3);
        if (!animationRules.isEmpty()) {
            QJsonObject animations;
            animations.insert(QStringLiteral("AnimationAppRules"), animationRules);
            root.insert(QStringLiteral("Animations"), animations);
        }
        return root;
    }

    QJsonObject makeShaderRule(const QString& classPattern, const QString& eventPath, const QString& effectId,
                               const QJsonObject& shaderParams = {})
    {
        QJsonObject o;
        o.insert(QStringLiteral("classPattern"), classPattern);
        o.insert(QStringLiteral("eventPath"), eventPath);
        o.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        o.insert(QStringLiteral("effectId"), effectId);
        if (!shaderParams.isEmpty()) {
            o.insert(QStringLiteral("shaderParams"), shaderParams);
        }
        return o;
    }

    QJsonObject makeTimingRule(const QString& classPattern, const QString& eventPath, const QString& curve,
                               int durationMs)
    {
        QJsonObject o;
        o.insert(QStringLiteral("classPattern"), classPattern);
        o.insert(QStringLiteral("eventPath"), eventPath);
        o.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        if (!curve.isEmpty()) {
            o.insert(QStringLiteral("curve"), curve);
        }
        if (durationMs > 0) {
            o.insert(QStringLiteral("durationMs"), durationMs);
        }
        return o;
    }

    /// All rules carrying an OverrideAnimation* action (either shader or
    /// timing). The animation rule set is otherwise independent of the
    /// assignment / disable cascade, so most tests want this filtered view.
    QList<QJsonObject> animationRulesFromRules()
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rulesFromRules()) {
            const QJsonObject rule = v.toObject();
            for (const QJsonValue& a : rule.value(QStringLiteral("actions")).toArray()) {
                const QString type = a.toObject().value(QStringLiteral("type")).toString();
                if (type == QLatin1String("overrideAnimationShader")
                    || type == QLatin1String("overrideAnimationTiming")) {
                    out.append(rule);
                    break;
                }
            }
        }
        return out;
    }

    /// First animation action on @p rule, or an empty object if none.
    QJsonObject animationAction(const QJsonObject& rule)
    {
        for (const QJsonValue& v : rule.value(QStringLiteral("actions")).toArray()) {
            const QJsonObject a = v.toObject();
            const QString type = a.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("overrideAnimationShader") || type == QLatin1String("overrideAnimationTiming")) {
                return a;
            }
        }
        return {};
    }

private Q_SLOTS:

    void testAnimationAppRules_shaderAndTimingKindsBecomeRules()
    {
        IsolatedConfigGuard guard;
        QJsonObject shaderParams;
        shaderParams.insert(QStringLiteral("amplitude"), 0.3);
        QJsonArray src;
        src.append(makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve"),
                                  shaderParams));
        src.append(makeTimingRule(QStringLiteral("Code"), QStringLiteral("window.close"),
                                  QStringLiteral("0.2,0.0,0.2,1.0"), 250));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 2);

        // Locate by action type — list ordering is asserted in a separate test.
        QJsonObject shaderRule;
        QJsonObject timingRule;
        for (const QJsonObject& rule : animRules) {
            const QJsonObject action = animationAction(rule);
            if (action.value(QStringLiteral("type")).toString() == QLatin1String("overrideAnimationShader")) {
                shaderRule = rule;
            } else {
                timingRule = rule;
            }
        }
        QVERIFY(!shaderRule.isEmpty());
        QVERIFY(!timingRule.isEmpty());

        // Action wire shape: `event`, `effectId`, `curve`, `durationMs`
        // live FLAT on the action object alongside `type` — there is no
        // outer wrapper named `params`. The shader case carries an INNER
        // `params` object that holds the effect-specific tunables
        // (amplitude, etc.); that nested key is the shader effect's own
        // payload, distinct from the action's flat wire params.

        // Shader rule shape.
        QCOMPARE(matchLeafValueByOp(shaderRule, QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("firefox"));
        QCOMPARE(shaderRule.value(QStringLiteral("enabled")).toBool(), true);
        QVERIFY2(shaderRule.value(QStringLiteral("priority")).toInt() > 0,
                 "animation app rules get a positive descending-by-list-order priority");
        const QJsonObject shaderAction = animationAction(shaderRule);
        QCOMPARE(shaderAction.value(QStringLiteral("event")).toString(), QStringLiteral("window.open"));
        QCOMPARE(shaderAction.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
        QCOMPARE(shaderAction.value(QStringLiteral("params")).toObject().value(QStringLiteral("amplitude")).toDouble(),
                 0.3);

        // Timing rule shape.
        QCOMPARE(matchLeafValueByOp(timingRule, QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("Code"));
        QCOMPARE(timingRule.value(QStringLiteral("enabled")).toBool(), true);
        QVERIFY(timingRule.value(QStringLiteral("priority")).toInt() > 0);
        const QJsonObject timingAction = animationAction(timingRule);
        QCOMPARE(timingAction.value(QStringLiteral("event")).toString(), QStringLiteral("window.close"));
        QCOMPARE(timingAction.value(QStringLiteral("curve")).toString(), QStringLiteral("0.2,0.0,0.2,1.0"));
        QCOMPARE(timingAction.value(QStringLiteral("durationMs")).toInt(), 250);
    }

    void testAnimationAppRules_priorityIsDescendingByListOrder()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Three valid entries — first should get the highest priority,
        // last should get priority 1 (descending by list order, lowest is 1).
        src.append(makeShaderRule(QStringLiteral("first"), QStringLiteral("window.open"), QStringLiteral("popup")));
        src.append(makeShaderRule(QStringLiteral("second"), QStringLiteral("window.open"), QStringLiteral("fade")));
        src.append(makeShaderRule(QStringLiteral("third"), QStringLiteral("window.open"), QStringLiteral("blur")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QMap<QString, int> priorityByPattern;
        for (const QJsonObject& rule : animationRulesFromRules()) {
            const QString pattern = matchLeafValueByOp(rule, QStringLiteral("windowClass"), QStringLiteral("contains"));
            priorityByPattern[pattern] = rule.value(QStringLiteral("priority")).toInt();
        }
        QCOMPARE(priorityByPattern.size(), 3);
        // count == 3 → first=3, second=2, third=1 (always > 0).
        QCOMPARE(priorityByPattern[QStringLiteral("first")], 3);
        QCOMPARE(priorityByPattern[QStringLiteral("second")], 2);
        QCOMPARE(priorityByPattern[QStringLiteral("third")], 1);
    }

    void testAnimationAppRules_engagedEmptyEffectIdPreserved()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Engaged-blocking sentinel: empty effectId means "no animation for
        // this app on this event" — must round-trip into the migrated rule
        // as an explicit empty-string effectId, NOT a missing key.
        src.append(makeShaderRule(QStringLiteral("krita"), QStringLiteral("window.open"), QString()));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 1);
        // Inlined action params — `effectId` lives directly on the action
        // object alongside `type` and `event`, not nested under `params`.
        const QJsonObject action = animationAction(animRules.first());
        QVERIFY2(action.contains(QStringLiteral("effectId")),
                 "engaged-blocking effectId must be present as the empty string, not absent");
        QCOMPARE(action.value(QStringLiteral("effectId")).toString(), QString());
    }

    void testAnimationAppRules_durationAndCurveSentinelsOmitted()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // durationMs <= 0 means "inherit per-event default" — the output
        // must omit the key entirely so the resolver falls through. Same
        // for an empty `curve`. Three sentinel shapes must all coalesce:
        //   - absent key       (the original / most common shape)
        //   - explicit `0`     (a user toggling between inherit and a
        //                       concrete value via the editor)
        //   - explicit negative (a hand-edited file or an editor bug)
        //
        // The production check is `durationMs > 0`, so a regression that
        // flipped it to `>= 0` or to `!= 0` would silently emit the key for
        // the explicit-zero / negative forms — invisible without a positive
        // test here.
        QJsonObject inheritAbsent;
        inheritAbsent.insert(QStringLiteral("classPattern"), QStringLiteral("vlc"));
        inheritAbsent.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritAbsent.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        // No `curve`, no `durationMs` — both inherit.
        src.append(inheritAbsent);

        QJsonObject inheritExplicitZero;
        inheritExplicitZero.insert(QStringLiteral("classPattern"), QStringLiteral("mpv"));
        inheritExplicitZero.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritExplicitZero.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        inheritExplicitZero.insert(QStringLiteral("durationMs"), 0); // explicit 0 → inherit
        src.append(inheritExplicitZero);

        QJsonObject inheritExplicitNegative;
        inheritExplicitNegative.insert(QStringLiteral("classPattern"), QStringLiteral("krita"));
        inheritExplicitNegative.insert(QStringLiteral("eventPath"), QStringLiteral("window.close"));
        inheritExplicitNegative.insert(QStringLiteral("kind"), QStringLiteral("timing"));
        inheritExplicitNegative.insert(QStringLiteral("durationMs"), -50); // explicit negative → inherit
        src.append(inheritExplicitNegative);

        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 3);
        for (const QJsonObject& rule : animRules) {
            const QJsonObject action = animationAction(rule);
            QCOMPARE(action.value(QStringLiteral("event")).toString(), QStringLiteral("window.close"));
            QVERIFY2(!action.contains(QStringLiteral("durationMs")),
                     "durationMs <= 0 is the inherit sentinel — must be absent from the migrated params "
                     "(verified for absent-key, explicit 0, and explicit negative shapes)");
            QVERIFY2(!action.contains(QStringLiteral("curve")),
                     "empty curve is the inherit sentinel — must be absent from the migrated params");
        }
    }

    void testAnimationAppRules_malformedEntriesDropped()
    {
        IsolatedConfigGuard guard;
        QJsonArray src;
        // Empty classPattern → dropped.
        src.append(makeShaderRule(QString(), QStringLiteral("window.open"), QStringLiteral("x")));
        // Empty eventPath → dropped.
        src.append(makeShaderRule(QStringLiteral("ok"), QString(), QStringLiteral("y")));
        // Unknown kind → dropped (silent coercion would be dangerous — an
        // unknown kind silently coerced to Shader produces an engaged-empty
        // rule that disables animations for matching windows).
        QJsonObject unknownKind;
        unknownKind.insert(QStringLiteral("classPattern"), QStringLiteral("gimp"));
        unknownKind.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        unknownKind.insert(QStringLiteral("kind"), QStringLiteral("xyzzy"));
        src.append(unknownKind);
        // Non-object entry → dropped (the source array element isn't an object).
        src.append(QJsonValue(QStringLiteral("not-an-object")));
        // Fully empty {} entry → all required keys missing → dropped at the
        // classPattern/eventPath emptiness gate.
        src.append(QJsonObject{});
        // classPattern is a number (non-string) → toString() returns ""
        // → dropped at the emptiness gate.
        QJsonObject nonStringPattern;
        nonStringPattern.insert(QStringLiteral("classPattern"), 42);
        nonStringPattern.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        nonStringPattern.insert(QStringLiteral("kind"), QStringLiteral("shader"));
        nonStringPattern.insert(QStringLiteral("effectId"), QStringLiteral("x"));
        src.append(nonStringPattern);
        // kind is missing entirely → unknown-kind branch → dropped.
        QJsonObject missingKind;
        missingKind.insert(QStringLiteral("classPattern"), QStringLiteral("inkscape"));
        missingKind.insert(QStringLiteral("eventPath"), QStringLiteral("window.open"));
        src.append(missingKind);
        // One valid entry survives.
        src.append(makeShaderRule(QStringLiteral("survivor"), QStringLiteral("window.open"), QStringLiteral("pop")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> animRules = animationRulesFromRules();
        QCOMPARE(animRules.size(), 1);
        QCOMPARE(matchLeafValueByOp(animRules.first(), QStringLiteral("windowClass"), QStringLiteral("contains")),
                 QStringLiteral("survivor"));
    }

    void testAnimationAppRules_absentSourceProducesNoAnimationRules()
    {
        IsolatedConfigGuard guard;
        // A v3 config with NO Animations.AnimationAppRules — the migration
        // must produce zero animation-action rules (other rules unaffected).
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QCOMPARE(animationRulesFromRules().size(), 0);
        // And no stash key persists.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QVERIFY(!cfg.contains(QStringLiteral("_v4AnimationRulesStash")));
    }

    void testAnimationAppRules_coexistWithAssignmentAndDisableRules()
    {
        // The animation rules and the assignment/disable rules target
        // disjoint slot namespaces, so a fixture combining both must produce
        // BOTH families in rules.json — and neither family clobbers
        // the other. This test is the load-bearing assertion for the
        // strip-after-rebuild path: the fixture populates
        // `_v4AnimationRulesStash`, so the post-conversion stash-absence
        // check actually verifies the rebuild branch stripped it (a
        // never-populated stash is trivially absent and verifies nothing).
        IsolatedConfigGuard guard;
        QJsonObject cfg = makeV3Config();
        QJsonArray animRules;
        animRules.append(
            makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        // Inline literals — independent-witness pin matching the
        // `makeV3ConfigWithAnimationRules` fixture pattern. The sibling
        // `makeV3ConfigWithAnimationRules` fixture's docstring documents
        // the rationale: a future freeze-policy violation that renames
        // `Legacy::v4AnimationAppRulesKey` away from "AnimationAppRules"
        // must surface as test breakage, not silently update through
        // the same accessor production uses.
        QJsonObject animations;
        animations.insert(QStringLiteral("AnimationAppRules"), animRules);
        cfg.insert(QStringLiteral("Animations"), animations);
        writeJson(ConfigDefaults::configFilePath(), cfg);
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Animation family — exactly one rule emitted.
        const QList<QJsonObject> animRulesOut = animationRulesFromRules();
        QCOMPARE(animRulesOut.size(), 1);
        const QJsonObject animRule = animRulesOut.first();
        // Animation rule sits at priority 1 (count == 1 → priority 1).
        QCOMPARE(animRule.value(QStringLiteral("priority")).toInt(), 1);

        // Assignment cascade — every fixture-pinned level (306/304/303/301)
        // must still produce an assignment rule (setEngineMode + NOT
        // disableEngine), matching testCascadePriorities_exactValues. A
        // regression where an animation rule collided with an assignment
        // rule's id would drop the assignment from the rebuilt set; counting
        // every pinned level guards that.
        const QJsonArray allRules = rulesFromRules();
        QVERIFY(hasAssignmentAtPriority(allRules, 306));
        QVERIFY(hasAssignmentAtPriority(allRules, 304));
        QVERIFY(hasAssignmentAtPriority(allRules, 303));
        QVERIFY(hasAssignmentAtPriority(allRules, 301));

        // Disable family — count must match testDisableListRules exactly
        // (fixture: 1 + 2 + 1 + 1 = 5). Drift here means an animation rule
        // clobbered a disable rule.
        QCOMPARE(disableRules(allRules).size(), 5);

        // Animation rule id is distinct from every other rule's id — disjoint
        // slot namespaces are nothing without disjoint identities (a clash on
        // (id) would overwrite a sibling rule inside RuleSet::addRule).
        const QString animRuleId = animRule.value(QStringLiteral("id")).toString();
        QVERIFY(!animRuleId.isEmpty());
        int collisions = 0;
        for (const QJsonValue& v : allRules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("id")).toString() == animRuleId && r != animRule) {
                ++collisions;
            }
        }
        QCOMPARE(collisions, 0);

        // The migration's scratch key MUST be stripped from config.json after
        // the rebuild branch consumed it. This fixture populates the stash
        // (animRules.size() > 0), so a regression that stopped stripping
        // would fail here — distinguishing this from the matching assertion
        // in testFullConversion_producesRules where the fixture never
        // populates the stash to begin with.
        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "the rebuild branch must strip _v4AnimationRulesStash after consuming it");
    }

    void testAnimationAppRules_idempotentRuleIds()
    {
        // Re-running the migration against a v3 fixture must yield
        // byte-identical animation rules — the rule id is derived from
        // (classPattern, eventPath, kind) via a fixed v5-UUID namespace, so
        // a repeat run produces the same UUID and the RuleSet round-trip
        // stays stable. This is the conversion's idempotency at the rule
        // level (the file-level idempotency is asserted by
        // testIdempotency_runTwiceIsNoOp above; this test scopes the assertion
        // tightly to the animation-rule output to catch a future drift in
        // the bridge's namespace UUID or segment-encoding).
        IsolatedConfigGuard guard;
        QJsonArray src;
        src.append(makeShaderRule(QStringLiteral("kitty"), QStringLiteral("window.open"), QStringLiteral("pop-in")));
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId = animationRulesFromRules().first().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());

        // Golden assertion: re-derive the expected id inline against the
        // SPEC of the namespace UUID + length-prefixed segment encoding.
        // The migration owns both ends of the v5-UUID derivation, so the
        // idempotency check above (same inputs → same id) cannot catch a
        // namespace-UUID or encoder change — both sides of that compare
        // drift together. Duplicating the namespace literal and the
        // segment-encoding format here means a deliberate change to either
        // forces a deliberate update to this test.
        const QUuid kExpectedNamespace(QStringLiteral("{b3f2c1a0-7d4e-5f6a-8b9c-0d1e2f3a4b5c}"));
        // Length-prefixed concatenation: <segment-size>:<segment-bytes> per
        // segment, no separator. Spelled out as three explicit segments so a
        // future input change here doesn't require recomputing digit lengths
        // by hand.
        //
        //   segment 1 → classPattern "kitty"           → "5:kitty"
        //   segment 2 → eventPath    "window.open"     → "11:window.open"
        //   segment 3 → kind         "shader"          → "6:shader"
        const QString kExpectedKey =
            QStringLiteral("5:kitty") + QStringLiteral("11:window.open") + QStringLiteral("6:shader");
        const QString expectedId = QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString();
        QCOMPARE(firstId, expectedId);

        // Wipe rules.json (force the rebuild path on next call) and
        // re-stage the same v3 inputs.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationRules(src));
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString secondId = animationRulesFromRules().first().value(QStringLiteral("id")).toString();
        QCOMPARE(secondId, firstId);
    }

    void testAnimationAppRules_cleanupOnlyBranchStripsStashes()
    {
        // The cleanup-only branch of finalizeV4Conversion runs when
        // rules.json already exists as a valid v4 rule set but the
        // previous run failed before stripping config.json's scratch keys.
        // A regression that stops stripping the stash on the cleanup retry
        // would leave inert _v4*Stash keys on disk forever — the rebuild
        // path is gated off (rules.json exists), so the main path
        // tested by other cases never re-runs to clean up.
        //
        // Fixture: a v4-stamped config.json that carries both stash keys,
        // plus an existing v4 rules.json (so rulesAlreadyConverted
        // returns true). ensureJsonConfigImpl's "Already at OR above current
        // version" branch still calls finalizeV4Conversion on every startup,
        // which takes the cleanup-only
        // sub-branch when the rule store already parses as v4 — strip both
        // stashes, leave rules.json byte-identical, never recreate
        // assignments.json or its .migrated quarantine artifact.
        IsolatedConfigGuard guard;

        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 4);
        // Plausible stash content for both keys — content is irrelevant to
        // the stripping logic; presence is what the predicate keys off.
        QJsonObject disableStash;
        disableStash.insert(QStringLiteral("snappingMonitors"), QStringLiteral("DP-1"));
        cfg.insert(QStringLiteral("_v4DisableStash"), disableStash);
        QJsonArray animStash;
        animStash.append(
            makeShaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        cfg.insert(QStringLiteral("_v4AnimationRulesStash"), animStash);
        // The exclusion stash uses the same Object-with-string-fields shape as
        // the disable stash. Plausible content for cleanup-branch coverage —
        // the actual conversion of these into Exclude rules is covered by
        // the dedicated testExclusions_* cases below.
        QJsonObject exclusionStash;
        exclusionStash.insert(QStringLiteral("Applications"), QStringLiteral("firefox"));
        exclusionStash.insert(QStringLiteral("WindowClasses"), QStringLiteral("kitty"));
        cfg.insert(QStringLiteral("_v4ExclusionStash"), exclusionStash);
        // Plausible animation-exclusion stash content too. Same shape as the
        // snapping exclusion stash (object with Applications / WindowClasses
        // string fields).
        QJsonObject animationExclusionStash;
        animationExclusionStash.insert(QStringLiteral("Applications"), QStringLiteral("vlc"));
        animationExclusionStash.insert(QStringLiteral("WindowClasses"), QStringLiteral("mpv"));
        cfg.insert(QStringLiteral("_v4AnimationExclusionStash"), animationExclusionStash);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        // Pre-existing rules.json: produced via the production save
        // path so the file shape always matches what `loadFromFile` expects
        // (hand-written JSON would silently drift if the library tightens
        // its load contract, flipping rulesAlreadyConverted to false
        // and silently rebuilding instead of taking the cleanup branch).
        PhosphorRules::RuleSet emptySet;
        QDir().mkpath(QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath());
        QVERIFY(emptySet.saveToFile(ConfigDefaults::rulesFilePath()));
        const QByteArray rulesBefore = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!rulesBefore.isEmpty());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Both stash keys gone from config.json.
        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4DisableStash")),
                 "cleanup-only branch must strip _v4DisableStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationRulesStash")),
                 "cleanup-only branch must strip _v4AnimationRulesStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4ExclusionStash")),
                 "cleanup-only branch must strip _v4ExclusionStash");
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationExclusionStash")),
                 "cleanup-only branch must strip _v4AnimationExclusionStash");

        // rules.json is byte-identical — the cleanup branch must NOT
        // rebuild and overwrite user-edited rules.
        const QByteArray rulesAfter = [&] {
            QFile f(ConfigDefaults::rulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(rulesAfter, rulesBefore);

        // Probe that the CLEANUP-ONLY branch was actually taken (not just
        // that the post-conditions happen to hold). The rebuild path writes
        // or quarantines assignments.json; the cleanup branch never touches
        // it. With no assignments.json staged in the fixture, the absence
        // of any assignments artifact after migration distinguishes the two
        // branches.
        QVERIFY2(!QFile::exists(assignmentsPath()), "cleanup-only branch must not recreate assignments.json");
        QVERIFY2(!QFile::exists(assignmentsPath() + QStringLiteral(".migrated")),
                 "cleanup-only branch must not produce an assignments.json.migrated artifact");
    }

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

    // ─── Animation exclusions fold ────────────────────────────────────────
    // The legacy `Animations.WindowFiltering.{Applications,WindowClasses}`
    // lists fold into `ExcludeAnimations`-action rules with
    // `DesktopFile Contains <pattern>` (applications) or
    // `WindowClass Contains <pattern>` (window classes) leaves — the
    // same match semantics the legacy effect-side bridge produced for
    // the animation pipeline, so an upgrading user's "no animations for
    // firefox" rule keeps the same matching behaviour.

private:
    QJsonObject makeV3ConfigWithAnimationExclusions(const QString& apps, const QString& windowClasses)
    {
        QJsonObject root;
        root.insert(QStringLiteral("_version"), 3);
        QJsonObject filtering;
        if (!apps.isNull()) {
            filtering.insert(QStringLiteral("Applications"), apps);
        }
        if (!windowClasses.isNull()) {
            filtering.insert(QStringLiteral("WindowClasses"), windowClasses);
        }
        if (!filtering.isEmpty()) {
            QJsonObject animations;
            animations.insert(QStringLiteral("WindowFiltering"), filtering);
            root.insert(QStringLiteral("Animations"), animations);
        }
        return root;
    }

    /// Animation-exclude rules are the only ExcludeAnimations-action shape the
    /// migration produces, with exactly one action, so filtering for the exact
    /// single-action list {excludeAnimations} cleanly isolates them. (Exact
    /// match, not "contains": if the migration ever emits a second action
    /// alongside it, this filter must be revisited rather than silently passing.)
    QList<QJsonObject> animationExclusionRules(const QJsonArray& rules)
    {
        QList<QJsonObject> out;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (actionTypes(r) != QStringList{QStringLiteral("excludeAnimations")}) {
                continue;
            }
            out.append(r);
        }
        return out;
    }

private Q_SLOTS:

    void testAnimationExclusions_applicationsBecomeDesktopFileContainsRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QStringLiteral("firefox,konsole"), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> rules = animationExclusionRules(rulesFromRules());
        QCOMPARE(rules.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : rules) {
            QCOMPARE(matchLeafValueByOp(r, QStringLiteral("desktopFile"), QStringLiteral("contains")).isEmpty(), false);
            patterns.append(matchLeafValueByOp(r, QStringLiteral("desktopFile"), QStringLiteral("contains")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox") << QStringLiteral("konsole"));
    }

    void testAnimationExclusions_windowClassesBecomeWindowClassContainsRules()
    {
        // Unlike the snapping-side fold (which collapsed both lists into
        // `AppId AppIdMatches` rules to mirror the daemon-bridge semantics),
        // the animation-side fold preserves the effect-bridge split:
        // WindowClasses entries produce `WindowClass Contains` leaves,
        // NOT `DesktopFile Contains`.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("kitty,org.kde.dolphin")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> rules = animationExclusionRules(rulesFromRules());
        QCOMPARE(rules.size(), 2);
        QStringList patterns;
        for (const QJsonObject& r : rules) {
            patterns.append(matchLeafValueByOp(r, QStringLiteral("windowClass"), QStringLiteral("contains")));
        }
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("kitty") << QStringLiteral("org.kde.dolphin"));
    }

    void testAnimationExclusions_emptyAndWhitespacePatternsDropped()
    {
        IsolatedConfigGuard guard;
        writeJson(
            ConfigDefaults::configFilePath(),
            makeV3ConfigWithAnimationExclusions(QStringLiteral(",firefox,  ,,konsole,"), QStringLiteral("   ,,")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(animationExclusionRules(rulesFromRules()).size(), 2);
    }

    void testAnimationExclusions_idempotentRuleIds()
    {
        // Counterpart to `testExclusions_idempotentRuleIds` — the
        // animation-side fold uses a DIFFERENT segment encoding (4 segments
        // including the action-type discriminator instead of the snapping
        // side's 3). The discriminator is what lets a future user-authored
        // Exclude rule and a migrated ExcludeAnimations rule share the same
        // (field, op, pattern) tuple without collapsing to the same id.
        // Pin the namespace + 4-segment shape inline so a future refactor
        // of either piece (namespace literal, Field/Operator enum value
        // renumbering, ActionType wire-string rename) has to update this
        // test deliberately rather than drift silently in both producer
        // and check.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("firefox")));
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString firstId =
            animationExclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QVERIFY(!firstId.isEmpty());

        // The namespace UUID is shared with the snapping-side fold — same
        // `appendExclusionRulesFromStash` / `appendAnimationExclusionRulesFromStash`
        // namespace constant in `configmigration.cpp::exclusionMigrationNamespace`.
        const QUuid kExpectedNamespace(QStringLiteral("{d5f4e3c2-9b60-7182-0abe-2f3a4b5c6d7e}"));
        // Segment encoding: <size>:<bytes> per segment, no separator.
        // The 4-segment shape for an animation WindowClass rule:
        //   field   = static_cast<int>(Field::WindowClass) = 1     → "1:1"
        //   op      = static_cast<int>(Operator::Contains) = 1     → "1:1"
        //   pattern = "firefox"                                    → "7:firefox"
        //   action  = "excludeAnimations" (17 bytes)               → "17:excludeAnimations"
        const QString kExpectedKey = QStringLiteral("1:1") + QStringLiteral("1:1") + QStringLiteral("7:firefox")
            + QStringLiteral("17:excludeAnimations");
        const QString expectedId = QUuid::createUuidV5(kExpectedNamespace, kExpectedKey).toString();
        QCOMPARE(firstId, expectedId);

        // Round-trip: wipe rules.json + re-stage same v3 input.
        // Re-running the migration on identical input must produce the
        // same id so `RuleStore::addRule`'s id-collision check
        // collapses the second run to a no-op instead of duplicating.
        QFile::remove(ConfigDefaults::rulesFilePath());
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QString(), QStringLiteral("firefox")));
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QString secondId =
            animationExclusionRules(rulesFromRules()).first().value(QStringLiteral("id")).toString();
        QCOMPARE(secondId, firstId);
    }

    void testAnimationExclusions_groupAndStashStrippedAfterFinalize()
    {
        // Both the v3 `Animations.WindowFiltering.{Applications,WindowClasses}`
        // leaf keys AND the `_v4AnimationExclusionStash` scratch root key
        // MUST be gone from config.json after a successful conversion.
        // The Animations group itself stays (it carries other v4 settings);
        // only the two leaf keys under WindowFiltering are stripped.
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(),
                  makeV3ConfigWithAnimationExclusions(QStringLiteral("firefox"), QStringLiteral("kitty")));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject cfgAfter = readJson(ConfigDefaults::configFilePath());
        QVERIFY2(!cfgAfter.contains(QStringLiteral("_v4AnimationExclusionStash")),
                 "finalizeV4Conversion must strip the _v4AnimationExclusionStash scratch key");
        // The Animations.WindowFiltering group either disappears entirely
        // (no other keys lived there in this fixture) or survives without
        // the two leaf keys. Both are acceptable; the assertion just
        // requires the leaf keys not survive.
        const QJsonObject animations = cfgAfter.value(QStringLiteral("Animations")).toObject();
        const QJsonObject filtering = animations.value(QStringLiteral("WindowFiltering")).toObject();
        QVERIFY2(!filtering.contains(QStringLiteral("Applications")),
                 "migrateV3ToV4 must remove Animations.WindowFiltering.Applications");
        QVERIFY2(!filtering.contains(QStringLiteral("WindowClasses")),
                 "migrateV3ToV4 must remove Animations.WindowFiltering.WindowClasses");
    }

    void testAnimationExclusions_absentSourceProducesNoRules()
    {
        IsolatedConfigGuard guard;
        // No Animations.WindowFiltering group at all.
        writeJson(ConfigDefaults::configFilePath(), makeV3ConfigWithAnimationExclusions(QString(), QString()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QCOMPARE(animationExclusionRules(rulesFromRules()).size(), 0);
    }

    // ─── Zone-overlay group rename: Snapping.Appearance.* → Snapping.Zones.* ─
    //
    // v3.1 renamed the "Snapping › Appearance" page (drag-time zone overlay)
    // to "Zones", moving its config groups so the freed Snapping.Appearance.*
    // namespace can hold the new snapped-window appearance settings. The move
    // folds into the EXISTING migrateV3ToV4 — no schema bump. These pins
    // assert the on-disk config.json shape; the four group/key names are
    // inline-literal pins so the test is an INDEPENDENT WITNESS of the rename.

private:
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

QTEST_MAIN(TestMigrationV3ToV4)
#include "test_migration_v3_to_v4.moc"

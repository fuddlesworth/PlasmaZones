// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4.cpp
 * @brief Unit tests for the v3 → v4 schema migration (window-rule
 *        consolidation) — assignment/disable cascade, superseding, and the
 *        data-loss failure paths.
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
 *
 * The animation folds live in test_migration_v3_to_v4_animations.cpp; the
 * exclusion fold and zone-overlay group rename live in
 * test_migration_v3_to_v4_exclusions.cpp. The shared config/rules JSON
 * helpers are in MigrationV3V4Fixture.h.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>
#include <QUuid>

#include <algorithm>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>

#include "MigrationV3V4Fixture.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace CRB = PhosphorRules::ContextRuleBridge;

class TestMigrationV3ToV4 : public QObject, public MigrationV3V4Fixture
{
    Q_OBJECT

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
        // byte-identical.
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
};

QTEST_MAIN(TestMigrationV3ToV4)
#include "test_migration_v3_to_v4.moc"

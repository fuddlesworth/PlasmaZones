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
 * test_migration_v3_to_v4_exclusions.cpp; the premade Steam rule and its
 * repair live in test_migration_v3_to_v4_steam.cpp; the malformed-input and
 * data-loss failure paths live in test_migration_v3_to_v4_failures.cpp. The
 * shared config/rules JSON helpers are in MigrationV3V4Fixture.h.
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
#include <optional>

#include "config/configdefaults.h"
#include "config/configkeys.h"
#include "config/configmigration.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/WindowQuery.h>

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

        // And it actually PRODUCED rules. The slot is named for that, but a
        // valid-but-rule-less store would satisfy the version stamp and the
        // stash-key absences below on its own.
        QVERIFY2(!rulesFromRules().isEmpty(), "the conversion must emit rules, not just a versioned shell");

        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        // The chain runs past the v3→v4 step to ConfigSchemaVersion, so
        // config.json lands at the current version whatever that is (the
        // v3→v4 step still stamps 4 mid-chain). Asserted against the constant
        // rather than a literal, so a future bump needs no edit here.
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

        // Same pattern in three layout files mapping to DIFFERENT zones. A
        // global ordinal SnapToZone rule fires regardless of the active
        // layout, so only the first in NAME order wins; the rest are dropped.
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
        // A THIRD file whose name sorts before both. With only two files the
        // winner is the same under name order, directory-enumeration order and
        // first-encountered, so the claim below would not be falsifiable.
        writeJson(layoutsDir + QStringLiteral("/0-layout.json"),
                  QJsonObject{{QStringLiteral("appRules"),
                               QJsonArray{QJsonObject{{QStringLiteral("pattern"), QStringLiteral("mpv")},
                                                      {QStringLiteral("zoneNumber"), 7}}}}});

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
        QCOMPARE(winningZones, (QList<int>{7})); // 0-layout.json wins on name order
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
        QVERIFY(convertFullV3Fixture());

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

        // Each priority is tied to the assignment that produced it, by the
        // dimensions its match pins. Asserting only that SOME assignment rule
        // exists at each value would pass just as well if the nudges were
        // scrambled across the four rules.
        const auto leafAt = [&](int priority, const QString& field) {
            return matchLeafValue(findAssignmentRuleByPriority(rules, priority), field);
        };

        // Exact (screen+desktop+activity) → 306.
        QCOMPARE(leafAt(306, QStringLiteral("screenId")), QStringLiteral("DP-2"));
        QCOMPARE(leafAt(306, QStringLiteral("virtualDesktop")), QStringLiteral("2"));
        QCOMPARE(leafAt(306, QStringLiteral("activity")), QStringLiteral("work-uuid"));

        // Screen + activity → 304 (activity nudge beats desktop).
        QCOMPARE(leafAt(304, QStringLiteral("screenId")), QStringLiteral("DP-2"));
        QCOMPARE(leafAt(304, QStringLiteral("activity")), QStringLiteral("play-uuid"));
        QVERIFY(leafAt(304, QStringLiteral("virtualDesktop")).isEmpty());

        // Screen + desktop → 303.
        QCOMPARE(leafAt(303, QStringLiteral("screenId")), QStringLiteral("DP-2"));
        QCOMPARE(leafAt(303, QStringLiteral("virtualDesktop")), QStringLiteral("3"));
        QVERIFY(leafAt(303, QStringLiteral("activity")).isEmpty());

        // Screen only → 301.
        QCOMPARE(leafAt(301, QStringLiteral("screenId")), QStringLiteral("DP-2"));
        QVERIFY(leafAt(301, QStringLiteral("virtualDesktop")).isEmpty());
        QVERIFY(leafAt(301, QStringLiteral("activity")).isEmpty());

        // Exactly four, so a spurious fifth assignment rule (or a nudge that
        // landed two of them on the same value) is caught rather than ignored.
        int assignmentCount = 0;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            const QStringList types = actionTypes(r);
            if (types.contains(QLatin1String("setEngineMode")) && !types.contains(QLatin1String("disableEngine"))) {
                ++assignmentCount;
            }
        }
        QCOMPARE(assignmentCount, 4);
    }

    // ─── Lossless three-action assignment rules ──────────────────────────

    void testAssignmentRule_carriesAllThreeActions()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertFullV3Fixture());

        const QJsonArray rules = rulesFromRules();

        // The exact rule (306) had Mode=Autotile + snappingLayout + tilingAlgo
        // — all three actions present. Disable rules all sit at the band base
        // (300), so none collides with an assignment rule; the
        // assignment-filtered lookup is used uniformly so the three sites stay
        // consistent.
        // The PAYLOADS are asserted alongside the type list, not just the
        // types. "Lossless" is a claim about the values: a conversion that
        // swapped snapping for autotile, emptied every layout id, or paired
        // the wrong layout with the wrong rule would satisfy a type-list-only
        // check exactly as well as a correct one does.
        const QJsonObject exact = findAssignmentRuleByPriority(rules, 306);
        QVERIFY(!exact.isEmpty());
        QCOMPARE(actionTypes(exact),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout"),
                              QStringLiteral("setTilingAlgorithm")}));
        QCOMPARE(actionParams(exact, QStringLiteral("setEngineMode")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("autotile"));
        QCOMPARE(actionParams(exact, QStringLiteral("setSnappingLayout")).value(QStringLiteral("layoutId")).toString(),
                 QStringLiteral("{snap-exact}"));
        QCOMPARE(
            actionParams(exact, QStringLiteral("setTilingAlgorithm")).value(QStringLiteral("algorithm")).toString(),
            QStringLiteral("dwindle"));

        // The screen+activity rule (304) had snapping mode + a layout, no
        // tiling algorithm → SetEngineMode + SetSnappingLayout only.
        const QJsonObject scrAct = findAssignmentRuleByPriority(rules, 304);
        QVERIFY(!scrAct.isEmpty());
        QCOMPARE(actionTypes(scrAct),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout")}));
        QCOMPARE(actionParams(scrAct, QStringLiteral("setEngineMode")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("snapping"));
        QCOMPARE(actionParams(scrAct, QStringLiteral("setSnappingLayout")).value(QStringLiteral("layoutId")).toString(),
                 QStringLiteral("{snap-act}"));

        // The screen+desktop rule (303) is the third distinct layout id, so a
        // conversion that reused one payload across rules is caught.
        const QJsonObject scrDesk = findAssignmentRuleByPriority(rules, 303);
        QVERIFY(!scrDesk.isEmpty());
        QCOMPARE(
            actionParams(scrDesk, QStringLiteral("setSnappingLayout")).value(QStringLiteral("layoutId")).toString(),
            QStringLiteral("{snap-desk}"));

        // The screen-only rule (301) was mode-only autotile (both layout
        // fields empty) → just SetEngineMode.
        const QJsonObject scrOnly = findAssignmentRuleByPriority(rules, 301);
        QVERIFY(!scrOnly.isEmpty());
        QCOMPARE(actionTypes(scrOnly), (QStringList{QStringLiteral("setEngineMode")}));
        QCOMPARE(actionParams(scrOnly, QStringLiteral("setEngineMode")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("autotile"));
    }

    // ─── Disable-list rules ───────────────────────────────────────────────

    void testDisableListRules()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertFullV3Fixture());

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
        QVERIFY(convertFullV3Fixture());

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

        // Tie the migration's OUTPUT to the bridge's identity derivation.
        // Asserting that makeDisableRule returns the priority it was handed
        // would be tautological — it assigns the argument straight through —
        // so pin the thing that can actually drift instead: the id the
        // migration writes for a (screen, desktop, activity, mode) tuple must
        // be the one the bridge derives for it, or a later run would emit a
        // duplicate rule beside the user's instead of recognising it.
        const auto ruleIdFor = [&](const QString& screenId, int desktop, const QString& activity, const QString& mode) {
            return CRB::disableRuleIdFor(screenId, desktop, activity, mode).toString();
        };
        QCOMPARE(snapDesktop->value(QStringLiteral("id")).toString(),
                 ruleIdFor(QStringLiteral("DP-1"), 4, QString(), QStringLiteral("snapping")));
        QCOMPARE(autotileActivity->value(QStringLiteral("id")).toString(),
                 ruleIdFor(QStringLiteral("DP-1"), 0, QStringLiteral("act-uuid-7"), QStringLiteral("autotile")));
    }

    // ─── Idempotency ──────────────────────────────────────────────────────

    void testIdempotency_runTwiceIsNoOp()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // Every file the conversion writes, not just rules.json: a second run
        // that reordered a config.json key or re-touched the quick-layout
        // sidecar would be invisible to a rules.json-only comparison.
        const auto readBytes = [](const QString& path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QStringList written{ConfigDefaults::rulesFilePath(), ConfigDefaults::configFilePath(),
                                  ConfigDefaults::quickLayoutsFilePath()};

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QList<QByteArray> firstRun;
        for (const QString& path : written) {
            firstRun.append(readBytes(path));
        }
        QVERIFY(!firstRun.at(0).isEmpty());

        // The process-level migration guard would normally short-circuit;
        // reset it so the second call re-runs the full logic against the
        // same (now-v4) config — which must be a clean no-op.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The `rulesAlreadyConverted` probe loads rules.json as a
        // v4 RuleSet; on the second run it succeeds, so finalize takes
        // the already-converted branch and only retries the idempotent
        // cleanup steps instead of rebuilding — every written file is
        // byte-identical.
        for (int i = 0; i < written.size(); ++i) {
            QVERIFY2(readBytes(written.at(i)) == firstRun.at(i), qPrintable(written.at(i)));
        }
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
        // location.
        QVERIFY(QFile::exists(ConfigDefaults::rulesFilePath()));
        QVERIFY2(!QFile::exists(assignmentsPath()), "assignments.json must be retired once rules.json supersedes it");

        // Retired NON-DESTRUCTIVELY. The rename to `.migrated` is the whole
        // point of preferring it over a delete: a downgrade or a manual
        // recovery needs the original bytes back. Asserting only that the old
        // path is gone would pass for a plain remove.
        const QString migrated = assignmentsPath() + QStringLiteral(".migrated");
        QVERIFY2(QFile::exists(migrated), "assignments.json must be renamed to .migrated, not deleted");
        QFile kept(migrated);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        const QJsonObject recovered = QJsonDocument::fromJson(kept.readAll()).object();
        kept.close();
        QCOMPARE(recovered, makeAssignments());
    }

    // ─── Superseding: Display.*Disabled* keys removed ─────────────────────

    /// migrateV3ToV4 removes the six config.json Display.*Disabled* keys for
    /// real — rules.json now carries them as DisableEngine rules, so a
    /// stale duplicate in config.json would be a split source of truth.
    void testSupersede_displayDisabledKeysRemoved()
    {
        IsolatedConfigGuard guard;
        QVERIFY(convertFullV3Fixture());

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
        QVERIFY(convertFullV3Fixture());

        const QString sidecar = ConfigDefaults::quickLayoutsFilePath();
        QVERIFY2(QFile::exists(sidecar), "QuickLayouts must be relocated to quicklayouts.json");
        const QJsonObject slots = readJson(sidecar);
        // Mode-nested format: v3 slots land under "snapping"; "autotile" is present and empty.
        const QJsonObject snapping = slots.value(QStringLiteral("snapping")).toObject();
        QCOMPARE(snapping.value(QStringLiteral("3")).toString(), QStringLiteral("{quick-layout-id}"));
        QVERIFY2(slots.contains(QStringLiteral("autotile")), "the nested format always carries both mode keys");
        QVERIFY2(!slots.contains(QStringLiteral("3")), "slots must be nested by mode, not written flat");

        // The QuickLayouts data must not have leaked into rules.json — it is
        // not a rule. Checked on the slot VALUE rather than on an action type
        // named "quickLayout": no such type exists in the vocabulary, so a
        // guard on it could never have failed whatever the migration did.
        const QJsonDocument rulesDoc(rulesFromRules());
        QVERIFY2(
            !QString::fromUtf8(rulesDoc.toJson(QJsonDocument::Compact)).contains(QStringLiteral("{quick-layout-id}")),
            "a QuickLayouts slot value must not appear anywhere in rules.json");
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
};

QTEST_MAIN(TestMigrationV3ToV4)
#include "test_migration_v3_to_v4.moc"

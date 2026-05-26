// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_migration_v3_to_v4.cpp
 * @brief Unit tests for the v3 → v4 schema migration (window-rule
 *        consolidation).
 *
 * A v3 config.json + assignments.json fixture is run through
 * ConfigMigration::ensureJsonConfig; the test asserts:
 *   - windowrules.json is produced at `_version == 4`,
 *   - each migrated zone Assignment becomes a context rule at the exact
 *     cascade priority dictated by the formula,
 *   - assignment rules carry SetEngineMode + (when non-empty)
 *     SetSnappingLayout / SetTilingAlgorithm,
 *   - a provider-default catch-all rule exists at priority 0,
 *   - per-mode disable-list entries become DisableEngine context rules,
 *   - config.json is stamped `_version == 4`,
 *   - the conversion is idempotent (running twice is a no-op).
 *
 * windowrules.json SUPERSEDES the v3 inputs: the migration renames
 * assignments.json to assignments.json.migrated after windowrules.json is
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
#include <QTest>

#include <algorithm>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configmigration.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
namespace CRB = PhosphorWindowRule::ContextRuleBridge;

class TestMigrationV3ToV4 : public QObject
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

    /// The legacy assignments.json path. ConfigDefaults no longer exposes it
    /// (windowrules.json supersedes it in v4) — it sits beside windowrules.json
    /// in the same plasmazones config directory.
    static QString assignmentsPath()
    {
        return QFileInfo(ConfigDefaults::windowRulesFilePath()).absolutePath() + QStringLiteral("/assignments.json");
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

        // Global default snapping layout — drives the provider-default rule.
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

    QJsonArray rulesFromWindowRules()
    {
        const QJsonObject root = readJson(ConfigDefaults::windowRulesFilePath());
        return root.value(QStringLiteral("rules")).toArray();
    }

    /// Returns the first rule at the given @p priority, or an empty object if
    /// none exists.
    QJsonObject findRuleByPriority(const QJsonArray& rules, int priority)
    {
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("priority")).toInt() == priority) {
                return r;
            }
        }
        return {};
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
        for (const QJsonObject& leaf : matchLeaves(rule)) {
            if (leaf.value(QStringLiteral("field")).toString() == field
                && leaf.value(QStringLiteral("op")).toString() == QLatin1String("equals")) {
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

    void testFullConversion_producesWindowRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // windowrules.json exists at _version 4.
        QVERIFY(QFile::exists(ConfigDefaults::windowRulesFilePath()));
        const QJsonObject wr = readJson(ConfigDefaults::windowRulesFilePath());
        QCOMPARE(wr.value(QStringLiteral("_version")).toInt(), 4);

        // config.json stamped v4.
        const QJsonObject cfg = readJson(ConfigDefaults::configFilePath());
        QCOMPARE(cfg.value(QStringLiteral("_version")).toInt(), 4);

        // The temporary stash key is stripped from config.json.
        QVERIFY(!cfg.contains(QStringLiteral("_v4DisableStash")));
    }

    // ─── Exact cascade priorities ─────────────────────────────────────────

    void testCascadePriorities_exactValues()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromWindowRules();

        // Each fixture Assignment migrated to a rule at the cascade priority
        // its pinned dimensions dictate. The four pinned levels must all be
        // present in the migrated rule set.
        //
        // Priorities 410/510 can also carry same-priority disable rules, so we
        // assert against the assignment cascade explicitly: a rule that sets an
        // engine mode and does NOT disable an engine.
        const auto isAssignmentRule = [this](const QJsonObject& rule) {
            const QStringList types = actionTypes(rule);
            return types.contains(QLatin1String("setEngineMode")) && !types.contains(QLatin1String("disableEngine"));
        };
        const auto hasAssignmentAtPriority = [&](int priority) {
            for (const QJsonObject& r : allRulesByPriority(rules, priority)) {
                if (isAssignmentRule(r)) {
                    return true;
                }
            }
            return false;
        };

        // Exact (screen+desktop+activity) → 610.
        QVERIFY(hasAssignmentAtPriority(610));
        // Screen + activity → 510 (activity weight beats desktop).
        QVERIFY(hasAssignmentAtPriority(510));
        // Screen + desktop → 410.
        QVERIFY(hasAssignmentAtPriority(410));
        // Screen only → 310.
        QVERIFY(hasAssignmentAtPriority(310));
    }

    // ─── Lossless three-action assignment rules ──────────────────────────

    void testAssignmentRule_carriesAllThreeActions()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromWindowRules();

        // The exact rule (610) had Mode=Autotile + snappingLayout + tilingAlgo
        // — all three actions present.
        const QJsonObject exact = findRuleByPriority(rules, 610);
        QVERIFY(!exact.isEmpty());
        QCOMPARE(actionTypes(exact),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout"),
                              QStringLiteral("setTilingAlgorithm")}));

        // The screen+activity rule (510) had snapping mode + a layout, no
        // tiling algorithm → SetEngineMode + SetSnappingLayout only.
        const QJsonObject scrAct = findRuleByPriority(rules, 510);
        QVERIFY(!scrAct.isEmpty());
        QCOMPARE(actionTypes(scrAct),
                 (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout")}));

        // The screen-only rule (310) was mode-only autotile (both layout
        // fields empty) → just SetEngineMode.
        const QJsonObject scrOnly = findRuleByPriority(rules, 310);
        QVERIFY(!scrOnly.isEmpty());
        QCOMPARE(actionTypes(scrOnly), (QStringList{QStringLiteral("setEngineMode")}));
    }

    // ─── Provider-default catch-all ───────────────────────────────────────

    void testProviderDefaultRule_atPriorityZero()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonArray rules = rulesFromWindowRules();
        const QList<QJsonObject> defaults = allRulesByPriority(rules, 0);
        QCOMPARE(defaults.size(), 1);

        const QJsonObject def = defaults.first();
        // The catch-all match is an empty All{} — serialized as { "all": [] }.
        const QJsonObject match = def.value(QStringLiteral("match")).toObject();
        QVERIFY(match.contains(QStringLiteral("all")));
        QVERIFY(match.value(QStringLiteral("all")).toArray().isEmpty());

        // The v3 config carried a DefaultLayoutId → provider default is a
        // snapping rule carrying that layout.
        QCOMPARE(actionTypes(def), (QStringList{QStringLiteral("setEngineMode"), QStringLiteral("setSnappingLayout")}));
    }

    // ─── Disable-list rules ───────────────────────────────────────────────

    void testDisableListRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> disabled = disableRules(rulesFromWindowRules());

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

    // ─── Multi-dimension disable-rule priority ────────────────────────────
    // The disable rules share the cascade priority formula with assignment
    // rules: a screen+desktop disable outranks a screen-only disable, and a
    // screen+activity disable outranks both. This exercises the formula above
    // the single-dimension (screen-only, 310) band — a monitor-only fixture
    // entry alone never reaches the multi-pin priorities.

    void testDisableRulePriority_multiDimension()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QList<QJsonObject> disabled = disableRules(rulesFromWindowRules());

        // The "DP-1/4" SnappingDisabledDesktops entry pins screen + desktop →
        // priority 410 (kBasePriority + screen + desktop weights).
        const auto snapDesktop = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("virtualDesktop")) == QLatin1String("4");
        });
        QVERIFY(snapDesktop != disabled.cend());
        QCOMPARE(snapDesktop->value(QStringLiteral("priority")).toInt(),
                 CRB::contextPriority(/*screenPinned=*/true, /*desktopPinned=*/true, /*activityPinned=*/false));
        QCOMPARE(snapDesktop->value(QStringLiteral("priority")).toInt(), 410);

        // The "DP-1/act-uuid-7" AutotileDisabledActivities entry pins screen +
        // activity → priority 510 (activity weight beats desktop).
        const auto autotileActivity = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("autotile")
                && matchLeafValue(r, QStringLiteral("activity")) == QLatin1String("act-uuid-7");
        });
        QVERIFY(autotileActivity != disabled.cend());
        QCOMPARE(autotileActivity->value(QStringLiteral("priority")).toInt(),
                 CRB::contextPriority(/*screenPinned=*/true, /*desktopPinned=*/false, /*activityPinned=*/true));
        QCOMPARE(autotileActivity->value(QStringLiteral("priority")).toInt(), 510);

        // A screen-only monitor disable sits at the single-dimension band (310).
        const auto snapMonitor = std::find_if(disabled.cbegin(), disabled.cend(), [&](const QJsonObject& r) {
            return disableActionMode(r) == QLatin1String("snapping")
                && matchLeafValue(r, QStringLiteral("screenId")) == QLatin1String("DP-3");
        });
        QVERIFY(snapMonitor != disabled.cend());
        QCOMPARE(snapMonitor->value(QStringLiteral("priority")).toInt(),
                 CRB::contextPriority(/*screenPinned=*/true, /*desktopPinned=*/false, /*activityPinned=*/false));

        // makeDisableRule's priority must agree with the migration output for
        // a multi-dimension entry — a screen+desktop disable rule pins 410.
        const PhosphorWindowRule::WindowRule directDesktop = CRB::makeDisableRule(
            QStringLiteral("d"), QStringLiteral("DP-1"), /*virtualDesktop=*/4, QString(), /*autotileMode=*/false);
        QCOMPARE(directDesktop.priority, 410);
        // A screen+activity disable rule pins 510 — activity outranks desktop.
        const PhosphorWindowRule::WindowRule directActivity = CRB::makeDisableRule(
            QStringLiteral("a"), QStringLiteral("DP-1"), 0, QStringLiteral("act-uuid-7"), /*autotileMode=*/true);
        QCOMPARE(directActivity.priority, 510);
        QVERIFY(directActivity.priority > directDesktop.priority);
    }

    // ─── Idempotency ──────────────────────────────────────────────────────

    void testIdempotency_runTwiceIsNoOp()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::windowRulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!firstRun.isEmpty());
        const int firstCount =
            QJsonDocument::fromJson(firstRun).object().value(QStringLiteral("rules")).toArray().size();

        // The process-level migration guard would normally short-circuit;
        // reset it so the second call re-runs the full logic against the
        // same (now-v4) config — which must be a clean no-op.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::windowRulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        // The `windowRulesAlreadyConverted` probe loads windowrules.json as a
        // v4 WindowRuleSet; on the second run it succeeds, so finalize takes
        // the already-converted branch and only retries the idempotent
        // cleanup steps instead of rebuilding — windowrules.json is
        // byte-identical, the rule count is unchanged.
        QCOMPARE(secondRun, firstRun);
        const int secondCount =
            QJsonDocument::fromJson(secondRun).object().value(QStringLiteral("rules")).toArray().size();
        QCOMPARE(secondCount, firstCount);
    }

    // ─── No-assignments fixture ───────────────────────────────────────────

    void testNoAssignments_stillWritesProviderDefault()
    {
        IsolatedConfigGuard guard;
        // A v3 config with no assignments.json at all.
        QJsonObject cfg;
        cfg.insert(QStringLiteral("_version"), 3);
        writeJson(ConfigDefaults::configFilePath(), cfg);

        QVERIFY(ConfigMigration::ensureJsonConfig());

        QVERIFY(QFile::exists(ConfigDefaults::windowRulesFilePath()));
        const QJsonArray rules = rulesFromWindowRules();
        // Exactly one rule: the provider-default catch-all.
        QCOMPARE(rules.size(), 1);
        const QJsonObject def = rules.first().toObject();
        QCOMPARE(def.value(QStringLiteral("priority")).toInt(), 0);

        // With no DefaultLayoutId and no Tiling default algorithm, the
        // provider default is the bare snapping placeholder: a single
        // SetEngineMode action (mode = "snapping"), no layout action — there
        // is no layout to carry, so SetSnappingLayout / SetTilingAlgorithm are
        // both absent.
        QCOMPARE(actionTypes(def), (QStringList{QStringLiteral("setEngineMode")}));
        const QJsonArray actions = def.value(QStringLiteral("actions")).toArray();
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first().toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    }

    // ─── Superseding: assignments.json deleted ────────────────────────────

    /// windowrules.json supersedes assignments.json — once the rule store is
    /// durably written, the legacy file is deleted (the irreversible commit).
    void testSupersede_assignmentsJsonDeleted()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(QFile::exists(assignmentsPath()));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        // windowrules.json written; assignments.json gone.
        QVERIFY(QFile::exists(ConfigDefaults::windowRulesFilePath()));
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "assignments.json must be deleted once windowrules.json supersedes it");
    }

    // ─── Superseding: Display.*Disabled* keys removed ─────────────────────

    /// migrateV3ToV4 removes the six config.json Display.*Disabled* keys for
    /// real — windowrules.json now carries them as DisableEngine rules, so a
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

    /// QuickLayouts slots are not window rules — the migration relocates them
    /// to the quicklayouts.json sidecar (the file LayoutRegistry reads), with
    /// the slot-number → layout-id shape preserved verbatim.
    void testSupersede_quickLayoutsRelocatedToSidecar()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QString sidecar = ConfigDefaults::quickLayoutsFilePath();
        QVERIFY2(QFile::exists(sidecar), "QuickLayouts must be relocated to quicklayouts.json");
        const QJsonObject slots = readJson(sidecar);
        QCOMPARE(slots.value(QStringLiteral("3")).toString(), QStringLiteral("{quick-layout-id}"));

        // The QuickLayouts data must not have leaked into windowrules.json as
        // a rule — it is not a rule.
        const QJsonArray rules = rulesFromWindowRules();
        for (const QJsonValue& v : rules) {
            QVERIFY(!actionTypes(v.toObject()).contains(QStringLiteral("quickLayout")));
        }
    }

    // ─── Idempotency of the superseding behaviour ─────────────────────────

    /// Running the migration a second time after assignments.json is already
    /// deleted is a clean no-op: the idempotency guard short-circuits on the
    /// existing v4 windowrules.json, nothing is re-created or re-deleted.
    void testSupersede_idempotentAfterAssignmentsDeleted()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(!QFile::exists(assignmentsPath()));
        const QByteArray firstRun = [&] {
            QFile f(ConfigDefaults::windowRulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(!firstRun.isEmpty());

        // Second run against the already-converted, assignments-less tree.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // assignments.json is not re-created; windowrules.json is byte-identical.
        QVERIFY(!QFile::exists(assignmentsPath()));
        const QByteArray secondRun = [&] {
            QFile f(ConfigDefaults::windowRulesFilePath());
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QCOMPARE(secondRun, firstRun);
    }

    // ─── Data-loss regression: delete-failure must not clobber the store ──
    //
    // Simulates the scenario where the assignments.json retire step fails (a
    // read-only filesystem, a lock, a permissions error) so the legacy file is
    // still present on the next startup. The conversion is already complete —
    // windowrules.json exists as a valid v4 WindowRuleSet, and the user has
    // since authored an extra rule via the rule editor. Re-running
    // finalizeV4Conversion MUST NOT rebuild-and-overwrite windowrules.json from
    // the dead assignments.json: the user's rule must survive.
    //
    // The fix gates the rebuild on `!windowRulesAlreadyConverted` (probed by
    // actually loading windowrules.json as a WindowRuleSet), NOT on
    // assignments.json's absence — so a permanently-undeletable assignments.json
    // can no longer clobber the rule store on every launch.
    void testDeleteFailure_doesNotOverwriteUserRules()
    {
        IsolatedConfigGuard guard;
        writeJson(ConfigDefaults::configFilePath(), makeV3Config());
        writeJson(assignmentsPath(), makeAssignments());

        // First run: full conversion produces windowrules.json.
        QVERIFY(ConfigMigration::ensureJsonConfig());
        QVERIFY(QFile::exists(ConfigDefaults::windowRulesFilePath()));

        // The user authors a new rule via the rule editor — load the store,
        // append a rule, persist it. This rule exists ONLY in windowrules.json;
        // it has no counterpart in assignments.json.
        auto setWithUserRule = PhosphorWindowRule::WindowRuleSet::loadFromFile(ConfigDefaults::windowRulesFilePath());
        QVERIFY2(setWithUserRule.has_value(), "windowrules.json must parse as a v4 rule set");
        const PhosphorWindowRule::WindowRule userRule =
            CRB::makeDisableRule(QStringLiteral("User-authored · DP-9"), QStringLiteral("DP-9"),
                                 /*virtualDesktop=*/0, QString(), /*autotileMode=*/false);
        const QUuid userRuleId = userRule.id;
        QVERIFY(setWithUserRule->addRule(userRule));
        QVERIFY(setWithUserRule->saveToFile(ConfigDefaults::windowRulesFilePath()));
        const int countWithUserRule = setWithUserRule->count();
        QVERIFY(countWithUserRule > 0);

        // Simulate the retire-step failure: assignments.json is still present
        // on disk (as if QFile::remove / rename had failed on the first run).
        // Without the fix, the old idempotency guard — gated on
        // assignments.json's absence — would NOT short-circuit, so the rebuild
        // path would re-run and overwrite windowrules.json, destroying the
        // user's rule.
        writeJson(assignmentsPath(), makeAssignments());
        QVERIFY(QFile::exists(assignmentsPath()));

        // Re-run the migration against the already-converted tree.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // The user's rule MUST survive — windowrules.json was not rebuilt.
        auto afterRerun = PhosphorWindowRule::WindowRuleSet::loadFromFile(ConfigDefaults::windowRulesFilePath());
        QVERIFY2(afterRerun.has_value(), "windowrules.json must still parse as a v4 rule set after the re-run");
        QVERIFY2(afterRerun->ruleById(userRuleId).has_value(),
                 "the user-authored rule must survive a re-run with assignments.json still present");
        QCOMPARE(afterRerun->count(), countWithUserRule);

        // The cleanup-only branch still retires the leftover assignments.json
        // (quarantined to .migrated, or deleted) so it cannot loop forever.
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "the cleanup-only branch must retire the leftover assignments.json");
    }

    // ─── Data-loss regression (B5): malformed assignments.json aborts ─────
    //
    // A corrupt assignments.json (truncation, power-loss, hand-edit error)
    // must NOT silently produce a windowrules.json holding only the provider-
    // default + disable rules — that would lose every pinned assignment AND
    // the quick-layout slots. The migration aborts loudly: the corrupt file
    // is quarantined to `.corrupt.bak` (NOT `.migrated`, which would imply a
    // successful migration), windowrules.json is NOT written, and config.json
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
            f.write(corruptBytes);
        }

        // Migration MUST abort.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false on a malformed assignments.json");

        // windowrules.json must NOT have been created — silently writing a
        // disable-only rule set would mask the data-loss.
        QVERIFY2(!QFile::exists(ConfigDefaults::windowRulesFilePath()),
                 "windowrules.json must not be written when the legacy sidecar is corrupt");

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

    // ─── Data-loss regression (B4): QuickLayouts write failure is recoverable ─
    //
    // The v3→v4 conversion writes two files: quicklayouts.json (sidecar) and
    // windowrules.json (the irreversible commit marker). Writing the sidecar
    // FIRST means a sidecar failure aborts BEFORE committing windowrules.json
    // — the user's slots stay recoverable from assignments.json on the next
    // attempt. Writing windowrules.json first would gate the rebuild path off
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
            pin.write("x");
        }

        // First attempt: the sidecar write fails, migration aborts BEFORE
        // committing windowrules.json. The legacy sidecar must still be on
        // disk (we never reach the retire step), so the data is recoverable.
        QVERIFY2(!ConfigMigration::ensureJsonConfig(),
                 "ensureJsonConfig must return false when the QuickLayouts sidecar write fails");
        QVERIFY2(!QFile::exists(ConfigDefaults::windowRulesFilePath()),
                 "windowrules.json must NOT be written when the sidecar write fails");
        QVERIFY2(QFile::exists(assignmentsPath()),
                 "assignments.json must remain on disk so the user can re-attempt the migration");

        // Recovery: remove the wedge (delete the pin file, then the
        // directory) so the next run can write the sidecar. The user's
        // environment is otherwise unchanged.
        QVERIFY(QFile::remove(quickLayoutsPath + QStringLiteral("/.pin")));
        QVERIFY(QDir().rmdir(quickLayoutsPath));
        QVERIFY(!QFileInfo::exists(quickLayoutsPath));

        // Second attempt: full migration succeeds. windowrules.json is
        // written, the sidecar is populated, and the legacy file is retired.
        ConfigMigration::resetMigrationGuardForTesting();
        QVERIFY2(ConfigMigration::ensureJsonConfig(),
                 "the migration must succeed once the QuickLayouts sidecar write can complete");
        QVERIFY(QFile::exists(ConfigDefaults::windowRulesFilePath()));
        QVERIFY2(QFile::exists(quickLayoutsPath), "the QuickLayouts sidecar must be populated on the second attempt");
        const QJsonObject slots = readJson(quickLayoutsPath);
        QCOMPARE(slots.value(QStringLiteral("3")).toString(), QStringLiteral("{quick-layout-id}"));
        QVERIFY2(!QFile::exists(assignmentsPath()),
                 "assignments.json must be retired once the full conversion completes");
    }
};

QTEST_MAIN(TestMigrationV3ToV4)
#include "test_migration_v3_to_v4.moc"

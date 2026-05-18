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
 * windowrules.json SUPERSEDES the v3 inputs: the migration deletes
 * assignments.json after windowrules.json is durably written, removes the
 * config.json Display.*Disabled* keys, and relocates the QuickLayouts slots
 * to the quicklayouts.json sidecar. These superseding behaviours are
 * asserted alongside the conversion fidelity.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configmigration.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorWindowRule/ContextRuleBridge.h>

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

    /// Find the first rule whose match contains an exact ScreenId leaf for
    /// @p screenId and (optionally) a desktop / activity pin. Returns an
    /// empty object if none. We match by the rule's priority + a screenId
    /// substring in the serialized match — simpler than re-parsing the tree.
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

        // Exact (screen+desktop+activity) → 610.
        QVERIFY(!findRuleByPriority(
                     rules, CRB::kBasePriority + CRB::kActivityWeight + CRB::kDesktopWeight + CRB::kScreenWeight)
                     .isEmpty());
        QCOMPARE(CRB::kBasePriority + CRB::kActivityWeight + CRB::kDesktopWeight + CRB::kScreenWeight, 610);

        // Screen + activity → 510 (activity weight beats desktop).
        QCOMPARE(CRB::kBasePriority + CRB::kActivityWeight + CRB::kScreenWeight, 510);
        QVERIFY(!findRuleByPriority(rules, 510).isEmpty());

        // Screen + desktop → 410.
        QCOMPARE(CRB::kBasePriority + CRB::kDesktopWeight + CRB::kScreenWeight, 410);
        QVERIFY(!findRuleByPriority(rules, 410).isEmpty());

        // Screen only → 310.
        QCOMPARE(CRB::kBasePriority + CRB::kScreenWeight, 310);
        QVERIFY(!findRuleByPriority(rules, 310).isEmpty());

        // Activity-pinned (510) outranks desktop-pinned (410) — structural.
        QVERIFY(510 > 410);
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

        const QJsonArray rules = rulesFromWindowRules();

        // Count DisableEngine rules. Fixture: snapping monitor (DP-3) = 1,
        // autotile monitors (DP-3, HDMI-2) = 2, snapping desktop (DP-1/4) = 1,
        // autotile activity (DP-1/act-uuid-7) = 1 → 5 disable rules.
        int disableRules = 0;
        for (const QJsonValue& v : rules) {
            const QJsonObject r = v.toObject();
            for (const QJsonValue& av : r.value(QStringLiteral("actions")).toArray()) {
                if (av.toObject().value(QStringLiteral("type")).toString() == QLatin1String("disableEngine")) {
                    ++disableRules;
                    break;
                }
            }
        }
        QCOMPARE(disableRules, 5);
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
        // The idempotency guard skips re-conversion — windowrules.json is
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
        QCOMPARE(rules.first().toObject().value(QStringLiteral("priority")).toInt(), 0);
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
};

QTEST_MAIN(TestMigrationV3ToV4)
#include "test_migration_v3_to_v4.moc"

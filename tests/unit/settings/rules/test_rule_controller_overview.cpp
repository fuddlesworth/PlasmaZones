// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_controller_overview.cpp
 * @brief Coverage for RuleController's read-only projection surfaces —
 *        the per-monitor overview summary and the curve-label resolver
 *        bridge. The authoring vocabulary (engine-mode picker, templates,
 *        action domains, input hints, default payloads) lives in
 *        test_rule_controller_vocabulary.cpp, split from here for file-size.
 *
 * Split out of test_rule_controller.cpp; the staging CRUD / dirty-tracking
 * contract stays with TestRuleController. Like that suite, every test here
 * constructs its own RuleController — in a headless unit run the daemon is
 * absent, so the model starts empty and the projection methods are exercised
 * against locally-staged rules.
 */

#include <QJSEngine>
#include <QTest>
#include <QUuid>

#include "settings/rules/rulecontroller.h"
#include "settings/rules/rulemodel.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

using namespace PlasmaZones;
using namespace PhosphorRules;

class TestRuleControllerOverview : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void monitorOverviewSummarises();
    void monitorOverviewIgnoresDisabledRules();
    void monitorOverviewClassifiesScrollingWithoutLayoutName();
    void monitorOverviewLayoutFollowsWinnerMode();
    void monitorOverviewIgnoresBareLayoutRules();
    void monitorOverviewIgnoresNonMonitorAxisDisableRules();
    void monitorOverviewScreenKeyFallback();
    void monitorOverviewLayoutFromSingleWinningRule();
    void monitorOverviewDisableEngineMatchesEffectiveMode();
    void monitorOverviewDisableEngineUnionsEveryMode();
    void monitorOverviewReportsLock();
    void monitorOverviewLockPriorityResolution();
    void curveLabelResolverBridgesQmlNaming();
};

void TestRuleControllerOverview::monitorOverviewSummarises()
{
    RuleController controller;

    // One rule pinned to DP-2 with an engine action.
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap match = rule.value(QStringLiteral("match")).toMap();
    match[QStringLiteral("value")] = QStringLiteral("DP-2");
    rule[QStringLiteral("match")] = match;
    rule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("autotile")}}};
    QVERIFY(!controller.addRuleFromJson(rule).isEmpty());

    // Two monitors connected — DP-2 has the rule, eDP-1 has none.
    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("eDP-1")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    bool sawDp2 = false;
    bool sawEdp1 = false;
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        if (tile.value(QStringLiteral("screenId")).toString() == QLatin1String("DP-2")) {
            sawDp2 = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
            QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), true);
        }
        if (tile.value(QStringLiteral("screenId")).toString() == QLatin1String("eDP-1")) {
            sawEdp1 = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 0);
            QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), false);
        }
    }
    QVERIFY(sawDp2);
    QVERIFY(sawEdp1);
}

void TestRuleControllerOverview::monitorOverviewReportsLock()
{
    // A LockContext rule pinning a monitor surfaces `locked: true` on its tile
    // (drives the lock badge), independent of any layout assignment — a
    // lock-only rule carries no SetEngineMode, so the tile has no layoutName,
    // yet it is locked. A rule whose lock value is false reports
    // `locked: false`, proving the tile reads the action's value (not mere
    // presence), mirroring resolveContextLocked.
    RuleController controller;

    // The helper RETURNS the new id rather than asserting on it: a QVERIFY
    // inside a void lambda returns from the lambda, not from the slot, so a
    // failed add would let the rest of the slot go on asserting against a
    // model it already knew was wrong.
    const auto lockRule = [&](const QString& screenId, bool locked) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = screenId;
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")}, {QStringLiteral("value"), locked}}};
        return controller.addRuleFromJson(rule);
    };
    QVERIFY(!lockRule(QStringLiteral("DP-2"), true).isEmpty());
    QVERIFY(!lockRule(QStringLiteral("DP-3"), false).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-3")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("eDP-1")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 3);

    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        const QString id = tile.value(QStringLiteral("screenId")).toString();
        if (id == QLatin1String("DP-2")) {
            // Lock-only rule: locked, counts as a pinned rule (assigned = has
            // any rule), and surfaces no layoutName (no engine-mode action).
            QCOMPARE(tile.value(QStringLiteral("locked")).toBool(), true);
            QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), true);
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
            QVERIFY(tile.value(QStringLiteral("layoutName")).toString().isEmpty());
        } else if (id == QLatin1String("DP-3")) {
            // value:false → not locked (the tile reads the value, not presence).
            QCOMPARE(tile.value(QStringLiteral("locked")).toBool(), false);
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
        } else if (id == QLatin1String("eDP-1")) {
            // No rule → not locked.
            QCOMPARE(tile.value(QStringLiteral("locked")).toBool(), false);
        }
    }
}

void TestRuleControllerOverview::monitorOverviewLockPriorityResolution()
{
    // When two opposing LockContext rules pin the SAME monitor, the tile must
    // report the HIGHEST-PRIORITY rule's value (first-wins), not last-wins and
    // not mere presence — mirroring resolveContextLocked's single-winner Locked
    // slot (cf. testContextLock_priorityResolution at the registry level).
    // Each added rule seeds at the bottom of its (Context) band tier, so within
    // a band the earlier-added rule keeps the higher global priority — the FIRST
    // rule added for a screen is its winner. Run both value directions so a
    // "true-always-wins" / "false-always-wins" bug fails one of the two.
    RuleController controller;

    const auto lockRule = [&](const QString& screenId, bool locked) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = screenId;
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")}, {QStringLiteral("value"), locked}}};
        return controller.addRuleFromJson(rule);
    };
    // DP-A: lock=true added FIRST (higher priority) over a later unlock → locked.
    // The adds are asserted at the CALL SITE, not inside the helper: a QVERIFY
    // in a void lambda returns from the lambda and lets the slot carry on
    // against a model it knows is short a rule.
    QVERIFY(!lockRule(QStringLiteral("DP-A"), true).isEmpty());
    QVERIFY(!lockRule(QStringLiteral("DP-A"), false).isEmpty());
    // DP-B: the inverse — unlock added first (higher priority) over a later
    // lock → not locked. Proves the winner is priority, not the value.
    QVERIFY(!lockRule(QStringLiteral("DP-B"), false).isEmpty());
    QVERIFY(!lockRule(QStringLiteral("DP-B"), true).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-A")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-B")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    bool sawA = false;
    bool sawB = false;
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        const QString id = tile.value(QStringLiteral("screenId")).toString();
        // Both screens carry two pinned lock rules.
        QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);
        if (id == QLatin1String("DP-A")) {
            sawA = true;
            QCOMPARE(tile.value(QStringLiteral("locked")).toBool(), true);
        } else if (id == QLatin1String("DP-B")) {
            sawB = true;
            QCOMPARE(tile.value(QStringLiteral("locked")).toBool(), false);
        }
    }
    // Pin identity: an unexpected screenId would otherwise skip both branches
    // and pass having verified no lock value.
    QVERIFY(sawA);
    QVERIFY(sawB);
}

void TestRuleControllerOverview::monitorOverviewIgnoresDisabledRules()
{
    RuleController controller;

    // A monitor-scoped rule that, while enabled, would pin DP-2's engine.
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap match = rule.value(QStringLiteral("match")).toMap();
    match[QStringLiteral("value")] = QStringLiteral("DP-2");
    rule[QStringLiteral("match")] = match;
    rule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("autotile")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());

    // Disable it. The daemon's RuleEvaluator skips !enabled rules, so the
    // overview must too — the tile contributes no rule and stays unassigned.
    QVERIFY(controller.setRuleEnabled(id, false));

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 1);
    const QVariantMap tile = overview.first().toMap();
    QCOMPARE(tile.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-2"));
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 0);
    QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), false);
}

void TestRuleControllerOverview::monitorOverviewClassifiesScrollingWithoutLayoutName()
{
    // Pin that a Scrolling-mode rule, even when carrying a stale snapping
    // layout in its action payload, produces an EMPTY `layoutName` on the
    // overview tile. The pre-Pass-3 inline `== "autotile"` / `== "snapping"`
    // classifier silently coerced Scrolling rules into the "no engine pin
    // → prefer snapping layout" fallback, mis-labelling the tile with the
    // leftover layout. A regression to that shape is caught here.
    RuleController controller;

    QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap match = rule.value(QStringLiteral("match")).toMap();
    match[QStringLiteral("value")] = QStringLiteral("DP-3");
    rule[QStringLiteral("match")] = match;
    // Build a SetEngineMode=scrolling action ALONGSIDE a stale snapping
    // layout payload — the bug class drops the SetEngineMode mode token
    // (silently mapping scrolling → snapping) and surfaces the snapping
    // layout as the tile's layoutName. With the fix, the classifier sees
    // mode=Scrolling and leaves layoutName empty.
    rule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("scrolling")}},
                     QVariantMap{{QStringLiteral("type"), QStringLiteral("setSnappingLayout")},
                                 {QStringLiteral("layoutId"), QStringLiteral("{stale-layout-id-not-real}")}}};
    QVERIFY(!controller.addRuleFromJson(rule).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-3")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 1);
    const QVariantMap tile = overview.first().toMap();
    QCOMPARE(tile.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-3"));
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), true);
    // The Scrolling branch yields no layout/algorithm to label — the tile
    // must read empty here, NOT the stale snapping layout id/name that
    // the pre-fix classifier would have surfaced.
    QVERIFY2(tile.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(tile.value(QStringLiteral("layoutName")).toString()));
}

void TestRuleControllerOverview::monitorOverviewLayoutFollowsWinnerMode()
{
    // The per-screen assignment winner (the rule carrying a SetEngineMode
    // action) supplies the engine mode AND both layout tokens; the tile shows
    // the token matching the winner's mode — mirroring the daemon's
    // resolveContextAssignment + entryFromRuleMatchActions (the AssignmentEntry
    // keeps both layouts, the active mode picks). The same rule shows its
    // snapping layout under Snapping and its algorithm under Autotile.
    RuleController controller;

    QVariantMap autoRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m1 = autoRule.value(QStringLiteral("match")).toMap();
    m1[QStringLiteral("value")] = QStringLiteral("DP-1");
    autoRule[QStringLiteral("match")] = m1;
    autoRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("autotile")}},
                     QVariantMap{{QStringLiteral("type"), QStringLiteral("setSnappingLayout")},
                                 {QStringLiteral("layoutId"), QStringLiteral("grid")}},
                     QVariantMap{{QStringLiteral("type"), QStringLiteral("setTilingAlgorithm")},
                                 {QStringLiteral("algorithm"), QStringLiteral("bsp")}}};
    QVERIFY(!controller.addRuleFromJson(autoRule).isEmpty());

    QVariantMap snapRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m2 = snapRule.value(QStringLiteral("match")).toMap();
    m2[QStringLiteral("value")] = QStringLiteral("DP-2");
    snapRule[QStringLiteral("match")] = m2;
    snapRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("snapping")}},
                     QVariantMap{{QStringLiteral("type"), QStringLiteral("setSnappingLayout")},
                                 {QStringLiteral("layoutId"), QStringLiteral("grid")}},
                     QVariantMap{{QStringLiteral("type"), QStringLiteral("setTilingAlgorithm")},
                                 {QStringLiteral("algorithm"), QStringLiteral("bsp")}}};
    QVERIFY(!controller.addRuleFromJson(snapRule).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    // Autotile winner → shows the tiling algorithm (both tokens kept; mode picks).
    const QVariantMap dp1 = overview.at(0).toMap();
    QCOMPARE(dp1.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-1"));
    QCOMPARE(dp1.value(QStringLiteral("layoutName")).toString(), QStringLiteral("bsp"));

    // Snapping winner on the SAME action shape → shows the snapping layout.
    const QVariantMap dp2 = overview.at(1).toMap();
    QCOMPARE(dp2.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-2"));
    QCOMPARE(dp2.value(QStringLiteral("layoutName")).toString(), QStringLiteral("grid"));
}

void TestRuleControllerOverview::monitorOverviewIgnoresBareLayoutRules()
{
    // A layout rule with NO SetEngineMode action is never the assignment winner
    // (the daemon's resolveContextAssignment filters to hasEngineModeAction), so
    // the daemon never applies its layout — and neither does the tile. The rule
    // still counts toward ruleCount/assigned (it targets the monitor), but
    // contributes no layout label, for both a bare tiling and a bare snapping rule.
    RuleController controller;

    QVariantMap algoRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m1 = algoRule.value(QStringLiteral("match")).toMap();
    m1[QStringLiteral("value")] = QStringLiteral("DP-1");
    algoRule[QStringLiteral("match")] = m1;
    algoRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setTilingAlgorithm")},
                                 {QStringLiteral("algorithm"), QStringLiteral("bsp")}}};
    QVERIFY(!controller.addRuleFromJson(algoRule).isEmpty());

    QVariantMap snapRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m2 = snapRule.value(QStringLiteral("match")).toMap();
    m2[QStringLiteral("value")] = QStringLiteral("DP-2");
    snapRule[QStringLiteral("match")] = m2;
    snapRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setSnappingLayout")},
                                 {QStringLiteral("layoutId"), QStringLiteral("grid")}}};
    QVERIFY(!controller.addRuleFromJson(snapRule).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-2")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    const QVariantMap dp1 = overview.at(0).toMap();
    // Pin the tile identity before asserting against it, matching the
    // sibling winner-mode test's idiom.
    QCOMPARE(dp1.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-1"));
    QCOMPARE(dp1.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp1.value(QStringLiteral("assigned")).toBool(), true);
    QVERIFY2(dp1.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(dp1.value(QStringLiteral("layoutName")).toString()));

    const QVariantMap dp2 = overview.at(1).toMap();
    QCOMPARE(dp2.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-2"));
    QCOMPARE(dp2.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp2.value(QStringLiteral("assigned")).toBool(), true);
    QVERIFY2(dp2.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(dp2.value(QStringLiteral("layoutName")).toString()));
}

void TestRuleControllerOverview::monitorOverviewIgnoresNonMonitorAxisDisableRules()
{
    // Mutation coverage for the DisableEngine narrowing gate
    // (rulecontroller_views.cpp: disableRuleMode + matchIsExactContextBase).
    // Both rejected shapes must leave tilingEnabled TRUE while the rule
    // still counts toward ruleCount — deleting either half of the gate
    // turns one of these tiles off and fails here.
    RuleController controller;

    // DP-A: a rule carrying TWO disableEngine actions — disableRuleMode
    // returns nullopt for the ambiguous shape.
    {
        QVariantMap modeRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = modeRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-A");
        modeRule[QStringLiteral("match")] = match;
        modeRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(modeRule).isEmpty());

        QVariantMap disableRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap dmatch = disableRule.value(QStringLiteral("match")).toMap();
        dmatch[QStringLiteral("value")] = QStringLiteral("DP-A");
        disableRule[QStringLiteral("match")] = dmatch;
        disableRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}},
                         QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                     {QStringLiteral("mode"), QStringLiteral("snapping")}}};
        QVERIFY(!controller.addRuleFromJson(disableRule).isEmpty());
    }

    // DP-B: a single disable action whose match pairs the screen leaf with a
    // second context leaf — contextAxisFor is not Monitor, so the gate
    // rejects it (the daemon does not honour it as a monitor disable).
    {
        QVariantMap modeRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = modeRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-B");
        modeRule[QStringLiteral("match")] = match;
        modeRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(modeRule).isEmpty());

        QVariantMap disableRule = controller.newEmptyRule(QStringLiteral("monitor"));
        // Composite wire shape: `{"all": [leaf, leaf]}` (matchexpression.cpp
        // kKeyAll), not a kind/children pair.
        disableRule[QStringLiteral("match")] =
            QVariantMap{{QStringLiteral("all"),
                         QVariantList{QVariantMap{{QStringLiteral("field"), QStringLiteral("screenId")},
                                                  {QStringLiteral("op"), QStringLiteral("equals")},
                                                  {QStringLiteral("value"), QStringLiteral("DP-B")}},
                                      QVariantMap{{QStringLiteral("field"), QStringLiteral("virtualDesktop")},
                                                  {QStringLiteral("op"), QStringLiteral("equals")},
                                                  {QStringLiteral("value"), 2}}}}};
        disableRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(disableRule).isEmpty());
    }

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-A")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-B")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);
    bool sawA = false;
    bool sawB = false;
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        const QString screenId = tile.value(QStringLiteral("screenId")).toString();
        if (screenId == QLatin1String("DP-A")) {
            sawA = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);
            QCOMPARE(tile.value(QStringLiteral("tilingEnabled")).toBool(), true);
        } else if (screenId == QLatin1String("DP-B")) {
            sawB = true;
            QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);
            QCOMPARE(tile.value(QStringLiteral("tilingEnabled")).toBool(), true);
        }
    }
    QVERIFY(sawA);
    QVERIFY(sawB);
}

void TestRuleControllerOverview::monitorOverviewScreenKeyFallback()
{
    // The tile loop's two-step key resolution: "name" wins, an entry with
    // only "screenId" falls back to it, and an entry with neither is
    // DROPPED — which silently changes overview.size(), so pin all three
    // shapes in one call.
    RuleController controller;
    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}},
                               QVariantMap{{QStringLiteral("screenId"), QStringLiteral("DP-2")}}, QVariantMap{}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);
    QCOMPARE(overview.at(0).toMap().value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-1"));
    QCOMPARE(overview.at(1).toMap().value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-2"));
}

void TestRuleControllerOverview::monitorOverviewLayoutFromSingleWinningRule()
{
    // Engine mode and layout must come from the SAME winning rule. An
    // engine-mode-only rule and a separate layout-only rule on one screen: the
    // engine rule is the assignment winner (only it has a SetEngineMode action),
    // and it carries no layout, so the tile shows NO layout — the other rule's
    // snapping layout never composes in (the daemon takes the whole entry from
    // the one winner). Both rules still count. The pre-fix independent-slot model
    // would have shown "grid" here.
    RuleController controller;

    QVariantMap engineRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m1 = engineRule.value(QStringLiteral("match")).toMap();
    m1[QStringLiteral("value")] = QStringLiteral("DP-1");
    engineRule[QStringLiteral("match")] = m1;
    engineRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                 {QStringLiteral("mode"), QStringLiteral("snapping")}}};
    QVERIFY(!controller.addRuleFromJson(engineRule).isEmpty());

    QVariantMap layoutRule = controller.newEmptyRule(QStringLiteral("monitor"));
    QVariantMap m2 = layoutRule.value(QStringLiteral("match")).toMap();
    m2[QStringLiteral("value")] = QStringLiteral("DP-1");
    layoutRule[QStringLiteral("match")] = m2;
    layoutRule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setSnappingLayout")},
                                 {QStringLiteral("layoutId"), QStringLiteral("grid")}}};
    QVERIFY(!controller.addRuleFromJson(layoutRule).isEmpty());

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 1);
    const QVariantMap tile = overview.first().toMap();
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);
    // Winner is the engine-only rule (snapping, no layout); the separate
    // snapping-layout rule's token must NOT leak into the tile.
    QVERIFY2(tile.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(tile.value(QStringLiteral("layoutName")).toString()));
}

void TestRuleControllerOverview::monitorOverviewDisableEngineMatchesEffectiveMode()
{
    // Pin that `tilingEnabled` on the overview tile resolves the
    // DisableEngine action against the screen's EFFECTIVE engine mode,
    // not "any DisableEngine action present". A DisableEngine{snapping}
    // rule on an Autotile-effective screen must NOT flip tilingEnabled
    // off — the cascade resolution in the daemon would never treat that
    // rule as disabling autotile. The matching positive case
    // (DisableEngine{mode} == effective mode) must flip it off.
    RuleController controller;
    // DP-A: SetEngineMode=autotile + DisableEngine=autotile → engine off.
    {
        QVariantMap modeRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = modeRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-A");
        modeRule[QStringLiteral("match")] = match;
        modeRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(modeRule).isEmpty());

        QVariantMap disableRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap dmatch = disableRule.value(QStringLiteral("match")).toMap();
        dmatch[QStringLiteral("value")] = QStringLiteral("DP-A");
        disableRule[QStringLiteral("match")] = dmatch;
        disableRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(disableRule).isEmpty());
    }
    // DP-B: SetEngineMode=autotile + DisableEngine=snapping → engine ON
    // (cross-mode disable must not flip the tile).
    {
        QVariantMap modeRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = modeRule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-B");
        modeRule[QStringLiteral("match")] = match;
        modeRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")},
                                     {QStringLiteral("mode"), QStringLiteral("autotile")}}};
        QVERIFY(!controller.addRuleFromJson(modeRule).isEmpty());

        QVariantMap disableRule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap dmatch = disableRule.value(QStringLiteral("match")).toMap();
        dmatch[QStringLiteral("value")] = QStringLiteral("DP-B");
        disableRule[QStringLiteral("match")] = dmatch;
        disableRule[QStringLiteral("actions")] =
            QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                     {QStringLiteral("mode"), QStringLiteral("snapping")}}};
        QVERIFY(!controller.addRuleFromJson(disableRule).isEmpty());
    }

    const QVariantList screens{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-A")}},
                               QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-B")}}};
    const QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);
    bool sawA = false;
    bool sawB = false;
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        const QString id = tile.value(QStringLiteral("screenId")).toString();
        if (id == QLatin1String("DP-A")) {
            sawA = true;
            QCOMPARE(tile.value(QStringLiteral("tilingEnabled")).toBool(), false);
        } else if (id == QLatin1String("DP-B")) {
            sawB = true;
            QCOMPARE(tile.value(QStringLiteral("tilingEnabled")).toBool(), true);
        }
    }
    QVERIFY(sawA);
    QVERIFY(sawB);
}

void TestRuleControllerOverview::monitorOverviewDisableEngineUnionsEveryMode()
{
    // The daemon's disable check is a per-mode UNION, not a single-winner slot:
    // it never runs a DisableEngine rule through RuleEvaluator, and
    // `Settings::disableEntriesFor` simply keeps every disable rule whose token
    // equals the mode it was asked about. One screen can therefore carry a
    // separate disable rule per engine — disabling a monitor for BOTH snapping
    // and autotile in the UI produces exactly that pair — and priority plays no
    // part in which of them counts.
    //
    // The autotile rule is added FIRST deliberately. addRuleFromJson runs
    // renormalizePriorities(), which re-stamps every rule as `rank * 16` in
    // store order, so the first-added autotile rule ends up ABOVE the snapping
    // one (32 vs 16) and the priority-DESC sort puts it first. A scalar
    // first-wins accumulator would therefore pin "autotile", drop the snapping
    // rule, and report the engine ON for a screen the daemon has switched off.
    RuleController controller;
    for (const QString& mode : {QStringLiteral("autotile"), QStringLiteral("snapping")}) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = QStringLiteral("DP-1");
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")}, {QStringLiteral("mode"), mode}}};
        QVERIFY(!controller.addRuleFromJson(rule).isEmpty());
    }

    // No SetEngineMode rule, so the screen's effective engine is the cascade's
    // Snapping default — and a snapping disable IS among the two.
    const QVariantList overview =
        controller.monitorOverview(QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("DP-1")}}});
    QCOMPARE(overview.size(), 1);
    const QVariantMap tile = overview.first().toMap();
    QVERIFY2(!tile.value(QStringLiteral("tilingEnabled")).toBool(),
             "a disable for a different engine masked the one matching the screen's effective mode");
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);
}

void TestRuleControllerOverview::curveLabelResolverBridgesQmlNaming()
{
    // The rule-list summary resolves OverrideAnimationCurve wire strings to
    // friendly names through a QML-supplied JS resolver (CurvePresets.curveLabel
    // in production). Exercise the actual QJSValue bridge end-to-end: install a
    // real engine-backed resolver and confirm the summary renders its output,
    // and that a non-callable value clears the resolver back to the raw value.
    //
    // The engine is declared BEFORE the controller so it outlives it: the
    // controller holds the installed QJSValue, and destruction runs in reverse
    // declaration order, so the other order would tear the engine down while
    // one of its values was still held by the controller.
    QJSEngine engine;
    RuleController controller;

    Rule curveRule;
    curveRule.id = QUuid::createUuid();
    curveRule.priority = 100;
    curveRule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
    RuleAction curve;
    curve.type = QString(ActionType::OverrideAnimationCurve);
    curve.params.insert(ActionParam::Curve, QStringLiteral("0.33,1.00,0.68,1.00"));
    curveRule.actions = {curve};
    controller.model()->setRules({curveRule});

    const auto summary = [&]() {
        return controller.model()->data(controller.model()->index(0, 0), RuleModel::ActionSummaryRole).toString();
    };

    // No resolver wired yet → the raw wire string round-trips behind the label.
    QCOMPARE(summary(), QStringLiteral("Curve: 0.33,1.00,0.68,1.00"));

    QJSValue resolver = engine.evaluate(
        QStringLiteral("(function(c){ return c === '0.33,1.00,0.68,1.00' ? 'Standard (Cubic)' : c; })"));
    QVERIFY(resolver.isCallable());
    controller.setCurveLabelResolver(resolver);
    QCOMPARE(summary(), QStringLiteral("Curve: Standard (Cubic)"));

    // A callable resolver returning an empty string falls back to the raw wire
    // value (the bridge's isEmpty() guard), not an empty "Curve: ".
    QJSValue emptyResolver = engine.evaluate(QStringLiteral("(function(c){ return ''; })"));
    QVERIFY(emptyResolver.isCallable());
    controller.setCurveLabelResolver(emptyResolver);
    QCOMPARE(summary(), QStringLiteral("Curve: 0.33,1.00,0.68,1.00"));

    // A non-callable value clears the resolver — the summary falls back to raw.
    // Re-install the working resolver first: the empty-resolver step above
    // already left the summary at the raw value, so clearing straight from there
    // asserted a no-op transition and stayed green even when the clear did
    // nothing at all. Going labelled → raw is the transition the contract is
    // about.
    controller.setCurveLabelResolver(resolver);
    QCOMPARE(summary(), QStringLiteral("Curve: Standard (Cubic)"));
    controller.setCurveLabelResolver(QJSValue());
    QCOMPARE(summary(), QStringLiteral("Curve: 0.33,1.00,0.68,1.00"));
}

QTEST_MAIN(TestRuleControllerOverview)

#include "test_rule_controller_overview.moc"

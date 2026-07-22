// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_controller_overview.cpp
 * @brief Coverage for RuleController's read-only projection surfaces —
 *        the per-monitor overview summary and the authoring vocabulary
 *        (engine-mode picker, templates, action domains, input hints,
 *        default payloads, and the curve-label resolver bridge).
 *
 * Split out of test_rule_controller.cpp; the staging CRUD / dirty-tracking
 * contract stays with TestRuleController. Like that suite, every test here
 * constructs its own RuleController — in a headless unit run the daemon is
 * absent, so the model starts empty and the projection methods are exercised
 * against locally-staged rules.
 */

#include <QJSEngine>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
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
    void monitorOverviewLayoutFromSingleWinningRule();
    void monitorOverviewDisableEngineMatchesEffectiveMode();
    void monitorOverviewDisableEngineUnionsEveryMode();
    void monitorOverviewReportsLock();
    void monitorOverviewLockPriorityResolution();
    void engineModePickerExposesAllVocabularyTokens();
    void inputHints();
    void templatesProduceSeededRules();
    void actionTypesCarryDomain();
    void defaultPayloadForSeedsParams();
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

    const auto lockRule = [&](const QString& screenId, bool locked) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = screenId;
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{
            QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")}, {QStringLiteral("value"), locked}}};
        QVERIFY(!controller.addRuleFromJson(rule).isEmpty());
    };
    lockRule(QStringLiteral("DP-2"), true);
    lockRule(QStringLiteral("DP-3"), false);

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
        QVERIFY(!controller.addRuleFromJson(rule).isEmpty());
    };
    // DP-A: lock=true added FIRST (higher priority) over a later unlock → locked.
    lockRule(QStringLiteral("DP-A"), true);
    lockRule(QStringLiteral("DP-A"), false);
    // DP-B: the inverse — unlock added first (higher priority) over a later
    // lock → not locked. Proves the winner is priority, not the value.
    lockRule(QStringLiteral("DP-B"), false);
    lockRule(QStringLiteral("DP-B"), true);

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
    QCOMPARE(dp1.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp1.value(QStringLiteral("assigned")).toBool(), true);
    QVERIFY2(dp1.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(dp1.value(QStringLiteral("layoutName")).toString()));

    const QVariantMap dp2 = overview.at(1).toMap();
    QCOMPARE(dp2.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp2.value(QStringLiteral("assigned")).toBool(), true);
    QVERIFY2(dp2.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(dp2.value(QStringLiteral("layoutName")).toString()));
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

void TestRuleControllerOverview::engineModePickerExposesAllVocabularyTokens()
{
    // Pin that the SetEngineMode + DisableEngine pickers expose exactly
    // three options — snapping / autotile / scrolling — with non-empty
    // localised labels. A regression that dropped the Scrolling enum
    // option from `engineModeOptions()` or the GPL settings-layer label
    // map would surface here.
    RuleController controller;
    const QVariantList types = controller.actionTypes();
    // Each entry in `actionTypes()` carries its own `params` list (see the
    // descriptor docstring in rulecontroller.h:330-344). For each
    // param of kind="enum", the `options` list contains `{value, label}`
    // pairs. We walk to the `mode` param of each action and extract its
    // options to verify the closed engine-mode vocabulary.
    const auto findModeOptions = [&](const QString& typeWire) -> QVariantList {
        for (const QVariant& t : types) {
            const QVariantMap tm = t.toMap();
            if (tm.value(QStringLiteral("value")).toString() != typeWire) {
                continue;
            }
            for (const QVariant& p : tm.value(QStringLiteral("params")).toList()) {
                const QVariantMap pm = p.toMap();
                if (pm.value(QStringLiteral("key")).toString() == QLatin1String("mode")) {
                    return pm.value(QStringLiteral("options")).toList();
                }
            }
        }
        return {};
    };
    for (const QString& actionWire : {QStringLiteral("setEngineMode"), QStringLiteral("disableEngine")}) {
        const QVariantList options = findModeOptions(actionWire);
        QCOMPARE(options.size(), 3);
        QStringList wireValues;
        for (const QVariant& opt : options) {
            const QVariantMap om = opt.toMap();
            wireValues.append(om.value(QStringLiteral("value")).toString());
            QVERIFY2(!om.value(QStringLiteral("label")).toString().isEmpty(),
                     qPrintable(QStringLiteral("empty label for %1 / %2")
                                    .arg(actionWire, om.value(QStringLiteral("value")).toString())));
        }
        QVERIFY2(wireValues.contains(QStringLiteral("snapping")), qPrintable(wireValues.join(QLatin1Char(','))));
        QVERIFY2(wireValues.contains(QStringLiteral("autotile")), qPrintable(wireValues.join(QLatin1Char(','))));
        QVERIFY2(wireValues.contains(QStringLiteral("scrolling")), qPrintable(wireValues.join(QLatin1Char(','))));
    }
}

void TestRuleControllerOverview::inputHints()
{
    RuleController controller;

    // Match-condition value hints are keyed on the operator wire token: the
    // operators whose value editor is a plain text box AND whose syntax /
    // matching semantics aren't obvious carry a hint; the self-explanatory ones
    // (and the picker / spin-box operators) carry none. Pins the QML contract —
    // MatchLeafEditor calls matchValueHint(node.op) and shows the result beneath
    // the value field — so a regression that drops or widens it is caught.
    QVERIFY2(!controller.matchValueHint(QStringLiteral("regex")).isEmpty(),
             "the regex operator must carry an input hint");
    QVERIFY2(!controller.matchValueHint(QStringLiteral("appIdMatches")).isEmpty(),
             "the app-id-match operator must carry an input hint");
    QVERIFY2(controller.matchValueHint(QStringLiteral("equals")).isEmpty(),
             "equals is self-explanatory and must carry no hint");
    QVERIFY2(controller.matchValueHint(QStringLiteral("contains")).isEmpty(),
             "contains is self-explanatory and must carry no hint");
    QVERIFY2(controller.matchValueHint(QString()).isEmpty(), "an empty operator token yields no hint");

    // The SnapToZone action's `zones` param carries an input hint (its free-text
    // ordinal-list syntax isn't discoverable from the field). The hint rides on
    // the param descriptor in actionTypes() — ActionRow reads `param.hint`.
    const QVariantList actions = controller.actionTypes();
    bool sawSnapToZoneZones = false;
    for (const QVariant& a : actions) {
        const QVariantMap action = a.toMap();
        if (action.value(QStringLiteral("value")).toString() != QLatin1String("snapToZone")) {
            continue;
        }
        for (const QVariant& p : action.value(QStringLiteral("params")).toList()) {
            const QVariantMap param = p.toMap();
            if (param.value(QStringLiteral("key")).toString() != QLatin1String("zones")) {
                continue;
            }
            sawSnapToZoneZones = true;
            QVERIFY2(!param.value(QStringLiteral("hint")).toString().isEmpty(),
                     "SnapToZone's zones param must carry an input hint");
        }
    }
    QVERIFY2(sawSnapToZoneZones, "actionTypes() must expose the SnapToZone zones param");
}

void TestRuleControllerOverview::templatesProduceSeededRules()
{
    RuleController controller;

    // The catalogue surfaced to the AddRuleSheet — every template entry
    // must carry the four UI fields the QML grid binds against. A missing
    // field would render a tile with a blank label or no icon.
    const QVariantList templates = controller.ruleTemplates();
    QVERIFY(!templates.isEmpty());
    for (const QVariant& v : templates) {
        const QVariantMap t = v.toMap();
        QVERIFY(!t.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("label")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("description")).toString().isEmpty());
        QVERIFY(!t.value(QStringLiteral("icon")).toString().isEmpty());
    }

    // `layoutOnMonitor` mirrors the old MonitorStatePage assignment flow:
    // ScreenId leaf + SetEngineMode("snapping") + SetSnappingLayout (empty
    // layoutId — the user fills it in the editor). The seeded action shape
    // is the rule editor's contract; regression there silently breaks the
    // quick-start flow.
    const QVariantMap layoutRule = controller.newRuleFromTemplate(QStringLiteral("layoutOnMonitor"));
    QVERIFY(!layoutRule.value(QStringLiteral("id")).toString().isEmpty());
    QCOMPARE(layoutRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("screenId"));
    const QVariantList layoutActions = layoutRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(layoutActions.size(), 2);
    QCOMPARE(layoutActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setEngineMode"));
    QCOMPARE(layoutActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    QCOMPARE(layoutActions.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setSnappingLayout"));

    // `algorithmOnMonitor` is the autotile mirror — same screen leaf,
    // SetEngineMode("autotile") + SetTilingAlgorithm.
    const QVariantMap algoRule = controller.newRuleFromTemplate(QStringLiteral("algorithmOnMonitor"));
    const QVariantList algoActions = algoRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(algoActions.size(), 2);
    QCOMPARE(algoActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("autotile"));
    QCOMPARE(algoActions.at(1).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("setTilingAlgorithm"));

    // `excludeApp` is the per-app exclusion template (Application subject +
    // Exclude action). Single action, no params required.
    const QVariantMap excludeRule = controller.newRuleFromTemplate(QStringLiteral("excludeApp"));
    QCOMPARE(excludeRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList excludeActions = excludeRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(excludeActions.size(), 1);
    QCOMPARE(excludeActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("exclude"));

    // `excludeSmallFromAnimations` showcases the new Width numeric match field:
    // a `Width LessThan 300` leaf + a single terminal ExcludeAnimations action.
    // Regression here means the quick-start that demonstrates the new fields is
    // broken or silently authoring the wrong predicate.
    const QVariantMap smallRule = controller.newRuleFromTemplate(QStringLiteral("excludeSmallFromAnimations"));
    const QVariantMap smallMatch = smallRule.value(QStringLiteral("match")).toMap();
    QCOMPARE(smallMatch.value(QStringLiteral("field")).toString(), QStringLiteral("width"));
    QCOMPARE(smallMatch.value(QStringLiteral("op")).toString(), QStringLiteral("lessThan"));
    QCOMPARE(smallMatch.value(QStringLiteral("value")).toInt(), 300);
    const QVariantList smallActions = smallRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(smallActions.size(), 1);
    QCOMPARE(smallActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("excludeAnimations"));

    // `lockLayoutOnMonitor` is the lock template: ScreenId leaf + a single
    // LockContext action seeded to lock (value == true). The seeded value is
    // the contract — a regression that drops/inverts it would author a rule
    // that silently doesn't lock, or doesn't surface in the picker's
    // value-on default.
    const QVariantMap lockRule = controller.newRuleFromTemplate(QStringLiteral("lockLayoutOnMonitor"));
    QVERIFY(!lockRule.value(QStringLiteral("id")).toString().isEmpty());
    QCOMPARE(lockRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("screenId"));
    const QVariantList lockActions = lockRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(lockActions.size(), 1);
    QCOMPARE(lockActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("lockContext"));
    QCOMPARE(lockActions.at(0).toMap().value(QStringLiteral("value")).toBool(), true);

    // `layoutOnDesktop` is the desktop twin of layoutOnMonitor: a
    // `VirtualDesktop == 1` leaf (0 is the all-desktops sentinel, so the seed
    // must be a real desktop number) + the same SetEngineMode("snapping") +
    // SetSnappingLayout pair.
    const QVariantMap desktopRule = controller.newRuleFromTemplate(QStringLiteral("layoutOnDesktop"));
    const QVariantMap desktopMatch = desktopRule.value(QStringLiteral("match")).toMap();
    QCOMPARE(desktopMatch.value(QStringLiteral("field")).toString(), QStringLiteral("virtualDesktop"));
    QCOMPARE(desktopMatch.value(QStringLiteral("value")).toInt(), 1);
    const QVariantList desktopActions = desktopRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(desktopActions.size(), 2);
    QCOMPARE(desktopActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    QCOMPARE(desktopActions.at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("setSnappingLayout"));

    // `portraitLayout` showcases the ScreenOrientation context field: a
    // `screenOrientation == "portrait"` leaf + the snapping assignment pair.
    const QVariantMap portraitRule = controller.newRuleFromTemplate(QStringLiteral("portraitLayout"));
    const QVariantMap portraitMatch = portraitRule.value(QStringLiteral("match")).toMap();
    QCOMPARE(portraitMatch.value(QStringLiteral("field")).toString(), QStringLiteral("screenOrientation"));
    QCOMPARE(portraitMatch.value(QStringLiteral("value")).toString(), QStringLiteral("portrait"));
    const QVariantList portraitActions = portraitRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(portraitActions.size(), 2);
    QCOMPARE(portraitActions.at(0).toMap().value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    QCOMPARE(portraitActions.at(1).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("setSnappingLayout"));

    // `snapAppToZone` is the flagship per-app placement rule: AppId leaf +
    // SnapToZone seeded with ordinal 1 (the validator requires a non-empty
    // zones list, so the seed must survive the round-trip).
    const QVariantMap snapRule = controller.newRuleFromTemplate(QStringLiteral("snapAppToZone"));
    QCOMPARE(snapRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList snapActions = snapRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(snapActions.size(), 1);
    QCOMPARE(snapActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("snapToZone"));
    const QVariantList seededZones = snapActions.at(0).toMap().value(QStringLiteral("zones")).toList();
    QCOMPARE(seededZones.size(), 1);
    QCOMPARE(seededZones.at(0).toInt(), 1);

    // `routeAppToScreen`: AppId leaf + RouteToScreen with an empty target
    // (the user picks the monitor in the editor).
    const QVariantMap routeRule = controller.newRuleFromTemplate(QStringLiteral("routeAppToScreen"));
    QCOMPARE(routeRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList routeActions = routeRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(routeActions.size(), 1);
    QCOMPARE(routeActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("routeToScreen"));

    // `floatApp`: AppId leaf + a single param-less Float action.
    const QVariantMap floatRule = controller.newRuleFromTemplate(QStringLiteral("floatApp"));
    QCOMPARE(floatRule.value(QStringLiteral("match")).toMap().value(QStringLiteral("field")).toString(),
             QStringLiteral("appId"));
    const QVariantList floatActions = floatRule.value(QStringLiteral("actions")).toList();
    QCOMPARE(floatActions.size(), 1);
    QCOMPARE(floatActions.at(0).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("float"));

    // An unknown id must return an empty map — the AddRuleSheet would
    // otherwise commit a UUID-less rule on a typo in the template id.
    const QVariantMap bogus = controller.newRuleFromTemplate(QStringLiteral("nonexistentTemplate"));
    QVERIFY(bogus.isEmpty());
}

void TestRuleControllerOverview::actionTypesCarryDomain()
{
    // The action row keys off this field to flag context-domain actions when
    // the match references window-property leaves — a regression that drops
    // the domain would silently lose the warning for the silently-never-fires
    // combination.
    RuleController controller;
    const QVariantList actions = controller.actionTypes();
    QVERIFY(!actions.isEmpty());
    QHash<QString, QString> domainOf;
    for (const QVariant& v : actions) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("value")).toString();
        const QString domain = m.value(QStringLiteral("domain")).toString();
        QVERIFY2(domain == QLatin1String("context") || domain == QLatin1String("window"),
                 qPrintable(QStringLiteral("action %1 has unexpected domain %2").arg(id, domain)));
        domainOf.insert(id, domain);
    }
    // Spot-check the canonical pairs — the context actions and a sample of
    // window actions. A typo in the descriptor would flip these.
    QCOMPARE(domainOf.value(QStringLiteral("setEngineMode")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("setSnappingLayout")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("setTilingAlgorithm")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("disableEngine")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("lockContext")), QStringLiteral("context"));
    QCOMPARE(domainOf.value(QStringLiteral("float")), QStringLiteral("window"));
    QCOMPARE(domainOf.value(QStringLiteral("exclude")), QStringLiteral("window"));
}

void TestRuleControllerOverview::defaultPayloadForSeedsParams()
{
    // The QML action row uses `defaultPayloadFor` when the user switches the
    // type picker to a new action — a stale regression that returned a bare
    // `{type: X}` map would leave SpinBoxes anchored at 0 and `canSave`
    // would gate the rule on params the user never had a chance to fill.
    RuleController controller;

    // Float carries no params — payload is exactly `{type: float}`.
    const QVariantMap floatPayload = controller.defaultPayloadFor(QStringLiteral("float"));
    QCOMPARE(floatPayload.value(QStringLiteral("type")).toString(), QStringLiteral("float"));
    QCOMPARE(floatPayload.size(), 1);

    // SetOpacity stores `display * scale`; the descriptor declares
    // defaultDisplay=100 with scale=0.01, so the seeded wire value is
    // 1.0 (100% — no visible change). A future change to the descriptor's
    // defaultDisplay would automatically flow through here. The earlier
    // seed-at-`min`=0 behaviour was a bug: a SetOpacity rule was savable
    // immediately at 0% (invisible window) before the user adjusted.
    const QVariantMap opacityPayload = controller.defaultPayloadFor(QStringLiteral("setOpacity"));
    QCOMPARE(opacityPayload.value(QStringLiteral("type")).toString(), QStringLiteral("setOpacity"));
    QVERIFY(opacityPayload.contains(QStringLiteral("value")));
    QCOMPARE(opacityPayload.value(QStringLiteral("value")).toDouble(), 1.0);

    // SetEngineMode's `mode` is an enum — seeded to the first option's wire
    // value. The engineModeOptions list begins with snapping, so the default
    // pre-selects that. Changing the order in the descriptor would
    // automatically change this default.
    const QVariantMap modePayload = controller.defaultPayloadFor(QStringLiteral("setEngineMode"));
    QCOMPARE(modePayload.value(QStringLiteral("type")).toString(), QStringLiteral("setEngineMode"));
    QCOMPARE(modePayload.value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));

    // SetSnappingLayout uses the `snappingLayout` picker kind — no implicit
    // default (the user must pick a layout), so the seeded value is an
    // empty string. `canSave` will then explicitly surface "layout missing".
    const QVariantMap layoutPayload = controller.defaultPayloadFor(QStringLiteral("setSnappingLayout"));
    QVERIFY(layoutPayload.contains(QStringLiteral("layoutId")));
    QCOMPARE(layoutPayload.value(QStringLiteral("layoutId")).toString(), QString());

    // OverrideAnimationCurve has two picker-kind params — both empty.
    const QVariantMap curvePayload = controller.defaultPayloadFor(QStringLiteral("overrideAnimationCurve"));
    QVERIFY(curvePayload.contains(QStringLiteral("event")));
    QVERIFY(curvePayload.contains(QStringLiteral("curve")));
    QCOMPARE(curvePayload.value(QStringLiteral("event")).toString(), QString());
    QCOMPARE(curvePayload.value(QStringLiteral("curve")).toString(), QString());

    // SetBorderColorActive / SetBorderColorInactive each carry a single
    // `color`-kind param keyed `value`, seeded with the accent sentinel so a
    // fresh border-colour rule follows the system accent until the user picks a
    // concrete colour.
    const QVariantMap borderColorActivePayload = controller.defaultPayloadFor(QStringLiteral("setBorderColorActive"));
    QCOMPARE(borderColorActivePayload.value(QStringLiteral("type")).toString(), QStringLiteral("setBorderColorActive"));
    QCOMPARE(borderColorActivePayload.value(QStringLiteral("value")).toString(), QStringLiteral("accent"));
    const QVariantMap borderColorInactivePayload =
        controller.defaultPayloadFor(QStringLiteral("setBorderColorInactive"));
    QCOMPARE(borderColorInactivePayload.value(QStringLiteral("type")).toString(),
             QStringLiteral("setBorderColorInactive"));
    QCOMPARE(borderColorInactivePayload.value(QStringLiteral("value")).toString(), QStringLiteral("accent"));

    // LockContext's `value` is a bool with defaultDisplay=1.0, so a freshly
    // type-switched lock action seeds to `value: true` — the picker opens
    // value-on (the meaningful "lock" default). A descriptor regression that
    // dropped defaultDisplay would seed `false` here, silently authoring a
    // lock action that doesn't lock.
    const QVariantMap lockPayload = controller.defaultPayloadFor(QStringLiteral("lockContext"));
    QCOMPARE(lockPayload.value(QStringLiteral("type")).toString(), QStringLiteral("lockContext"));
    QVERIFY(lockPayload.contains(QStringLiteral("value")));
    QCOMPARE(lockPayload.value(QStringLiteral("value")).toBool(), true);

    // Unknown type → bare `{type: X}` map. The QML side will never call this
    // with an unknown wire (the picker only offers registered types), but
    // returning a sane shape keeps the contract total.
    const QVariantMap unknownPayload = controller.defaultPayloadFor(QStringLiteral("bogusActionType"));
    QCOMPARE(unknownPayload.value(QStringLiteral("type")).toString(), QStringLiteral("bogusActionType"));
    QCOMPARE(unknownPayload.size(), 1);
}

void TestRuleControllerOverview::curveLabelResolverBridgesQmlNaming()
{
    // The rule-list summary resolves OverrideAnimationCurve wire strings to
    // friendly names through a QML-supplied JS resolver (CurvePresets.curveLabel
    // in production). Exercise the actual QJSValue bridge end-to-end: install a
    // real engine-backed resolver and confirm the summary renders its output,
    // and that a non-callable value clears the resolver back to the raw value.
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

    QJSEngine engine;
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

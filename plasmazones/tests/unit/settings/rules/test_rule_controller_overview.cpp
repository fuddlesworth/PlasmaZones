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

#include <QFile>
#include <QJSEngine>
#include <QLocale>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

#include "settings/rules/rulecontroller.h"
#include "settings/rules/rulemodel.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

using namespace PlasmaZones;
using namespace PhosphorRules;

namespace {

/// A leaf predicate in the rule-JSON wire shape the editor writes.
QVariantMap leafJson(const QString& field, const QString& op, const QVariant& value)
{
    return QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("op"), op}, {QStringLiteral("value"), value}};
}

/// Stage a monitor-section rule with @p match and @p actions; the new id, or
/// empty when the controller refused it.
QString addRule(RuleController& controller, const QVariantMap& match, const QVariantList& actions)
{
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("monitor"));
    rule[QStringLiteral("match")] = match;
    rule[QStringLiteral("actions")] = actions;
    return controller.addRuleFromJson(rule);
}

/// A `SettingsController::screens` entry. A zero size leaves the device-pixel
/// keys out, as ScreenHelper does when the daemon answered none. @p known is
/// ScreenHelper's `pixelSizeKnown`: false for the daemon's estimate.
QVariantMap screenJson(const QString& name, int pixelWidth = 0, int pixelHeight = 0, bool known = true)
{
    QVariantMap screen{{QStringLiteral("name"), name}};
    if (pixelWidth > 0 && pixelHeight > 0) {
        screen[QStringLiteral("pixelWidth")] = pixelWidth;
        screen[QStringLiteral("pixelHeight")] = pixelHeight;
        screen[QStringLiteral("pixelSizeKnown")] = known;
    }
    return screen;
}

/// An engine-mode action plus that engine's layout action.
QVariantList assignActions(const QString& mode, const QString& layoutType, const QString& layoutKey,
                           const QString& layoutValue)
{
    return {QVariantMap{{QStringLiteral("type"), QStringLiteral("setEngineMode")}, {QStringLiteral("mode"), mode}},
            QVariantMap{{QStringLiteral("type"), layoutType}, {layoutKey, layoutValue}}};
}

/// A one-action rule that changes nothing the tile shows, for counting.
QVariantList gapActions()
{
    return {QVariantMap{{QStringLiteral("type"), QStringLiteral("setInnerGap")}, {QStringLiteral("value"), 4}}};
}

/// The tile for @p screenId in @p overview, or an empty map.
QVariantMap tileFor(const QVariantList& overview, const QString& screenId)
{
    for (const QVariant& v : overview) {
        const QVariantMap tile = v.toMap();
        if (tile.value(QStringLiteral("screenId")).toString() == screenId) {
            return tile;
        }
    }
    return {};
}

} // namespace

class TestRuleControllerOverview : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void monitorOverviewSummarises();
    void monitorOverviewIgnoresDisabledRules();
    void monitorOverviewClassifiesScrollingWithoutLayoutName();
    void monitorOverviewLayoutFollowsWinnerMode();
    void monitorOverviewAppliesBareLayoutRules();
    void monitorOverviewIgnoresNonMonitorAxisDisableRules();
    void monitorOverviewScreenKeyFallback();
    void monitorOverviewLayoutComposesPerSlot();
    void monitorOverviewDisableEngineMatchesEffectiveMode();
    void monitorOverviewDisableEngineUnionsEveryMode();
    void monitorOverviewDisableEngineFollowsDefaultMode();
    void monitorOverviewReportsLock();
    void monitorOverviewLockPriorityResolution();
    void monitorOverviewShapeConditionFollowsCurrentSize();
    void monitorOverviewLockFollowsCurrentShape();
    void monitorOverviewCountsOnlyRulesThatApplyNow();
    void monitorOverviewTiledCountComesFromScreenState();
    void monitorOverviewVirtualScreenFallsBackToMonitorRules();
    void monitorOverviewVirtualScreenJudgesMonitorRulesByItsOwnShape();
    void monitorOverviewEstimatedSizeIsUnknown();
    void monitorOverviewColorSchemeFromProvider();
    void monitorOverviewCountsOnlyLiteralMonitorPins();
    void screenSizeHintNamesPinnedMonitors();
    void screenSizeHintNamesVirtualScreensOfAPinnedMonitor();
    void curveLabelResolverBridgesQmlNaming();
    void rulesSnapshotFeedsEveryRuleRowRequiredProperty();
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

void TestRuleControllerOverview::monitorOverviewAppliesBareLayoutRules()
{
    // A layout rule with NO SetEngineMode action fills its own slot: the
    // daemon's resolveAssignmentEntry bases the entry on the global default
    // and lets the payload winner override that field. So the tile shows the
    // bare layout when the default runs the matching engine, and nothing of it
    // when the default runs another one.
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

    // No default provider: the base is an empty Snapping entry. DP-2's bare
    // snapping layout applies, DP-1's bare algorithm is held for an engine
    // the screen does not run.
    QVariantList overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);

    QVariantMap dp1 = overview.at(0).toMap();
    // Pin the tile identity before asserting against it, matching the
    // sibling winner-mode test's idiom.
    QCOMPARE(dp1.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-1"));
    QCOMPARE(dp1.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp1.value(QStringLiteral("assigned")).toBool(), true);
    QVERIFY2(dp1.value(QStringLiteral("layoutName")).toString().isEmpty(),
             qPrintable(dp1.value(QStringLiteral("layoutName")).toString()));

    QVariantMap dp2 = overview.at(1).toMap();
    QCOMPARE(dp2.value(QStringLiteral("screenId")).toString(), QStringLiteral("DP-2"));
    QCOMPARE(dp2.value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(dp2.value(QStringLiteral("assigned")).toBool(), true);
    QCOMPARE(dp2.value(QStringLiteral("layoutName")).toString(), QStringLiteral("grid"));

    // An Autotile default: DP-1's bare algorithm now applies, and DP-2's
    // bare snapping layout is held while the screen tiles with the default
    // algorithm.
    controller.setDefaultAssignmentProvider([]() {
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        entry.tilingAlgorithm = QStringLiteral("columns");
        return entry;
    });
    overview = controller.monitorOverview(screens);
    QCOMPARE(overview.size(), 2);
    dp1 = overview.at(0).toMap();
    QCOMPARE(dp1.value(QStringLiteral("layoutName")).toString(), QStringLiteral("bsp"));
    dp2 = overview.at(1).toMap();
    QCOMPARE(dp2.value(QStringLiteral("layoutName")).toString(), QStringLiteral("columns"));
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
    // DP-B sits on desktop 2, so its screen + desktop disable rule matches and
    // counts. It is still not a monitor-axis disable.
    const QVariantList states{
        QVariantMap{{QStringLiteral("screenId"), QStringLiteral("DP-B")}, {QStringLiteral("virtualDesktop"), 2}}};
    const QVariantList overview = controller.monitorOverview(screens, states);
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

void TestRuleControllerOverview::monitorOverviewLayoutComposesPerSlot()
{
    // The four assignment slots resolve independently, as the daemon's
    // resolveRuleSlots does. An engine-mode-only rule and a separate
    // layout-only rule on one screen: the engine rule decides the mode and the
    // layout rule fills the snapping-layout slot, so the screen runs "grid"
    // in Snapping mode. Both rules count.
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
    QCOMPARE(tile.value(QStringLiteral("layoutName")).toString(), QStringLiteral("grid"));
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

void TestRuleControllerOverview::monitorOverviewDisableEngineFollowsDefaultMode()
{
    // A screen no rule assigns runs the global default's engine, so a
    // monitor-axis disable for THAT engine turns the tile off. The mode comes
    // from the default-assignment provider, as the daemon's comes from its
    // level-1 default.
    RuleController controller;
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("disableEngine")},
                                              {QStringLiteral("mode"), QStringLiteral("autotile")}}})
                 .isEmpty());
    const QVariantList screens{screenJson(QStringLiteral("DP-1"))};

    // Without a provider the base is Snapping, which the rule does not disable.
    QCOMPARE(tileFor(controller.monitorOverview(screens), QStringLiteral("DP-1"))
                 .value(QStringLiteral("tilingEnabled"))
                 .toBool(),
             true);

    controller.setDefaultAssignmentProvider([]() {
        PhosphorZones::AssignmentEntry entry;
        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
        entry.tilingAlgorithm = QStringLiteral("bsp");
        return entry;
    });
    QCOMPARE(tileFor(controller.monitorOverview(screens), QStringLiteral("DP-1"))
                 .value(QStringLiteral("tilingEnabled"))
                 .toBool(),
             false);
}

void TestRuleControllerOverview::monitorOverviewShapeConditionFollowsCurrentSize()
{
    // The headline two-rule case: a higher-priority rule for the monitor's
    // wide mode over a plain rule for the same monitor. The tile shows the
    // wide layout only while the screen is that wide, and counts the
    // conditional rule only then. With no size known the size condition does
    // not hold, as in the daemon. The first-added rule ranks higher.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{
                         {QStringLiteral("all"),
                          QVariantList{
                              leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                              leafJson(QStringLiteral("screenWidth"), QStringLiteral("greaterThan"), 3000)}}},
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("wide")))
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("standard")))
                 .isEmpty());

    QVariantMap tile =
        tileFor(controller.monitorOverview({screenJson(QStringLiteral("DP-1"), 5120, 1440)}), QStringLiteral("DP-1"));
    QCOMPARE(tile.value(QStringLiteral("layoutName")).toString(), QStringLiteral("wide"));
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 2);

    tile =
        tileFor(controller.monitorOverview({screenJson(QStringLiteral("DP-1"), 2560, 1440)}), QStringLiteral("DP-1"));
    QCOMPARE(tile.value(QStringLiteral("layoutName")).toString(), QStringLiteral("standard"));
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);

    tile = tileFor(controller.monitorOverview({screenJson(QStringLiteral("DP-1"))}), QStringLiteral("DP-1"));
    QCOMPARE(tile.value(QStringLiteral("layoutName")).toString(), QStringLiteral("standard"));
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 1);
}

void TestRuleControllerOverview::monitorOverviewLockFollowsCurrentShape()
{
    // The lock badge resolves through the daemon's lock resolver against the
    // same current shape. DP-1 locks only in its 32:9 mode. DP-2 locks while
    // portrait, and its orientation comes from the device-pixel size, which
    // decides it over the logical geometry exactly as the daemon's
    // screenShape does (the logical size here is deliberately landscape).
    RuleController controller;
    const QVariantList lock{
        QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")}, {QStringLiteral("value"), true}}};
    QVERIFY(!addRule(controller,
                     QVariantMap{{QStringLiteral("all"),
                                  QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                        QStringLiteral("DP-1")),
                                               leafJson(QStringLiteral("screenAspectRatio"), QStringLiteral("equals"),
                                                        QStringLiteral("32:9"))}}},
                     lock)
                 .isEmpty());
    QVERIFY(!addRule(controller,
                     QVariantMap{{QStringLiteral("all"),
                                  QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                        QStringLiteral("DP-2")),
                                               leafJson(QStringLiteral("screenOrientation"), QStringLiteral("equals"),
                                                        QStringLiteral("portrait"))}}},
                     lock)
                 .isEmpty());

    QVariantMap rotated = screenJson(QStringLiteral("DP-2"), 1440, 2560);
    rotated[QStringLiteral("width")] = 2560;
    rotated[QStringLiteral("height")] = 1440;

    QVariantList overview = controller.monitorOverview({screenJson(QStringLiteral("DP-1"), 5120, 1440), rotated});
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("locked")).toBool(), true);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("locked")).toBool(), true);

    overview = controller.monitorOverview({screenJson(QStringLiteral("DP-1"), 2560, 1440)});
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("locked")).toBool(), false);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("ruleCount")).toInt(), 0);
}

void TestRuleControllerOverview::monitorOverviewCountsOnlyRulesThatApplyNow()
{
    // A rule counts toward a tile when it names a monitor outside a negation
    // and its whole match holds for that screen now. `none{ScreenId}`
    // excludes a monitor rather than addressing one, so it counts nowhere. A
    // ScreenId compares ignoring case, as the rule engine's Equals does. A
    // desktop condition holds only on the desktop the screen state reports.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{{QStringLiteral("none"),
                                  QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                        QStringLiteral("DP-1"))}}},
                     gapActions())
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("dp-1")),
                     gapActions())
                 .isEmpty());
    QVERIFY(
        !addRule(controller,
                 QVariantMap{{QStringLiteral("all"),
                              QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                    QStringLiteral("DP-1")),
                                           leafJson(QStringLiteral("virtualDesktop"), QStringLiteral("equals"), 2)}}},
                 gapActions())
             .isEmpty());

    const QVariantList screens{screenJson(QStringLiteral("DP-1")), screenJson(QStringLiteral("DP-2"))};
    const auto statesOnDesktop = [](int desktop) {
        return QVariantList{QVariantMap{{QStringLiteral("screenId"), QStringLiteral("DP-1")},
                                        {QStringLiteral("virtualDesktop"), desktop}},
                            QVariantMap{{QStringLiteral("screenId"), QStringLiteral("DP-2")},
                                        {QStringLiteral("virtualDesktop"), desktop}}};
    };

    QVariantList overview = controller.monitorOverview(screens, statesOnDesktop(1));
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("ruleCount")).toInt(), 0);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("assigned")).toBool(), false);

    overview = controller.monitorOverview(screens, statesOnDesktop(2));
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("ruleCount")).toInt(), 2);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("ruleCount")).toInt(), 0);
}

void TestRuleControllerOverview::monitorOverviewTiledCountComesFromScreenState()
{
    // The algorithm-switch rule the TiledWindowCount field exists for: a
    // higher-priority rule once a second window tiles, over a plain one. The
    // count comes from the screen state, and its absence (a screen that is
    // not tiling) leaves the condition unanswered.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{
                         {QStringLiteral("all"),
                          QVariantList{
                              leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                              leafJson(QStringLiteral("tiledWindowCount"), QStringLiteral("greaterThan"), 1)}}},
                     assignActions(QStringLiteral("autotile"), QStringLiteral("setTilingAlgorithm"),
                                   QStringLiteral("algorithm"), QStringLiteral("bsp")))
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("autotile"), QStringLiteral("setTilingAlgorithm"),
                                   QStringLiteral("algorithm"), QStringLiteral("monocle")))
                 .isEmpty());

    const QVariantList screens{screenJson(QStringLiteral("DP-1"))};
    const auto stateWithCount = [](const QVariant& count) {
        QVariantMap state{{QStringLiteral("screenId"), QStringLiteral("DP-1")}, {QStringLiteral("virtualDesktop"), 1}};
        if (count.isValid()) {
            state[QStringLiteral("tiledWindowCount")] = count;
        }
        return QVariantList{state};
    };
    QCOMPARE(tileFor(controller.monitorOverview(screens, stateWithCount(3)), QStringLiteral("DP-1"))
                 .value(QStringLiteral("layoutName"))
                 .toString(),
             QStringLiteral("bsp"));
    QCOMPARE(tileFor(controller.monitorOverview(screens, stateWithCount(1)), QStringLiteral("DP-1"))
                 .value(QStringLiteral("layoutName"))
                 .toString(),
             QStringLiteral("monocle"));
    QCOMPARE(tileFor(controller.monitorOverview(screens, stateWithCount(QVariant())), QStringLiteral("DP-1"))
                 .value(QStringLiteral("layoutName"))
                 .toString(),
             QStringLiteral("monocle"));
}

void TestRuleControllerOverview::monitorOverviewVirtualScreenFallsBackToMonitorRules()
{
    // A virtual screen no rule names takes its monitor's assignment, as the
    // daemon's resolveWithScreenFallback retries the physical id. The rule
    // names the monitor, not this screen, so it does not count here.
    RuleController controller;
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("grid")))
                 .isEmpty());
    const QString virtualId = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("DP-1"), 0);
    const QVariantMap tile = tileFor(controller.monitorOverview({screenJson(virtualId)}), virtualId);
    QCOMPARE(tile.value(QStringLiteral("layoutName")).toString(), QStringLiteral("grid"));
    QCOMPARE(tile.value(QStringLiteral("assigned")).toBool(), true);
    QCOMPARE(tile.value(QStringLiteral("ruleCount")).toInt(), 0);
}

void TestRuleControllerOverview::monitorOverviewVirtualScreenJudgesMonitorRulesByItsOwnShape()
{
    // A monitor split into a wide and a narrow virtual screen: both inherit
    // the monitor's rules through the physical-id retry, and the retry judges
    // a screen-size condition against each virtual screen's own size, as the
    // daemon's does. Only the wide one takes the wide layout.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{
                         {QStringLiteral("all"),
                          QVariantList{
                              leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                              leafJson(QStringLiteral("screenWidth"), QStringLiteral("greaterThan"), 3000)}}},
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("wide")))
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("standard")))
                 .isEmpty());

    const QString wideId = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("DP-1"), 0);
    const QString narrowId = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("DP-1"), 1);
    const QVariantList overview =
        controller.monitorOverview({screenJson(wideId, 3840, 1440), screenJson(narrowId, 1280, 1440)});
    QCOMPARE(tileFor(overview, wideId).value(QStringLiteral("layoutName")).toString(), QStringLiteral("wide"));
    QCOMPARE(tileFor(overview, narrowId).value(QStringLiteral("layoutName")).toString(), QStringLiteral("standard"));
}

void TestRuleControllerOverview::monitorOverviewEstimatedSizeIsUnknown()
{
    // Before the compositor's first report, with no size from an earlier
    // session, the daemon's getScreenInfo carries an estimate that its own
    // rules do not see. The tile treats it the same way: a size condition
    // does not hold on it, and the orientation comes from the logical
    // geometry (landscape here) rather than from the estimate (portrait).
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{
                         {QStringLiteral("all"),
                          QVariantList{
                              leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                              leafJson(QStringLiteral("screenWidth"), QStringLiteral("greaterThan"), 3000)}}},
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("wide")))
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("standard")))
                 .isEmpty());
    QVERIFY(!addRule(controller,
                     QVariantMap{{QStringLiteral("all"),
                                  QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                        QStringLiteral("DP-2")),
                                               leafJson(QStringLiteral("screenOrientation"), QStringLiteral("equals"),
                                                        QStringLiteral("portrait"))}}},
                     QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("lockContext")},
                                              {QStringLiteral("value"), true}}})
                 .isEmpty());

    QVariantMap tall = screenJson(QStringLiteral("DP-2"), 1440, 2560, false);
    tall[QStringLiteral("width")] = 2560;
    tall[QStringLiteral("height")] = 1440;
    const QVariantList estimated{screenJson(QStringLiteral("DP-1"), 5120, 1440, false), tall};

    QVariantList overview = controller.monitorOverview(estimated);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("layoutName")).toString(),
             QStringLiteral("standard"));
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("locked")).toBool(), false);
    QVERIFY(controller
                .screenSizeHint(estimated,
                                leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")))
                .isEmpty());

    // The same sizes once known decide both.
    tall[QStringLiteral("pixelSizeKnown")] = true;
    overview = controller.monitorOverview({screenJson(QStringLiteral("DP-1"), 5120, 1440), tall});
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("layoutName")).toString(),
             QStringLiteral("wide"));
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("locked")).toBool(), true);
}

void TestRuleControllerOverview::monitorOverviewColorSchemeFromProvider()
{
    // A ColorScheme condition reads the scheme the provider answers on each
    // call, so the tile follows a light/dark flip without a rule edit.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     QVariantMap{{QStringLiteral("all"),
                                  QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                        QStringLiteral("DP-1")),
                                               leafJson(QStringLiteral("colorScheme"), QStringLiteral("equals"),
                                                        QStringLiteral("dark"))}}},
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("night")))
                 .isEmpty());
    QVERIFY(!addRule(controller, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                     assignActions(QStringLiteral("snapping"), QStringLiteral("setSnappingLayout"),
                                   QStringLiteral("layoutId"), QStringLiteral("day")))
                 .isEmpty());
    const QVariantList screens{screenJson(QStringLiteral("DP-1"))};
    const auto layoutNow = [&controller, &screens]() {
        return tileFor(controller.monitorOverview(screens), QStringLiteral("DP-1"))
            .value(QStringLiteral("layoutName"))
            .toString();
    };

    QCOMPARE(layoutNow(), QStringLiteral("day"));
    QString scheme = QStringLiteral("dark");
    controller.setColorSchemeProvider([&scheme]() {
        return scheme;
    });
    QCOMPARE(layoutNow(), QStringLiteral("night"));
    scheme = QStringLiteral("light");
    QCOMPARE(layoutNow(), QStringLiteral("day"));
    controller.setColorSchemeProvider({});
}

void TestRuleControllerOverview::monitorOverviewCountsOnlyLiteralMonitorPins()
{
    // A tile counts the rules that address its monitor by a whole ScreenId
    // (RuleModel::screenIdsOf, the walk the Rules page's monitor filter
    // reads). A substring pin addresses no one monitor, so it counts on no
    // tile even where it matches, and a monitor excluded inside a `none`
    // group is not addressed by the rule.
    RuleController controller;
    QVERIFY(!addRule(controller,
                     leafJson(QStringLiteral("screenId"), QStringLiteral("startsWith"), QStringLiteral("DP")),
                     gapActions())
                 .isEmpty());
    QVERIFY(!addRule(controller,
                     QVariantMap{
                         {QStringLiteral("all"),
                          QVariantList{
                              leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                              QVariantMap{{QStringLiteral("none"),
                                           QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                                 QStringLiteral("DP-2"))}}}}}},
                     gapActions())
                 .isEmpty());

    const QVariantList overview =
        controller.monitorOverview({screenJson(QStringLiteral("DP-1")), screenJson(QStringLiteral("DP-2"))});
    QCOMPARE(tileFor(overview, QStringLiteral("DP-1")).value(QStringLiteral("ruleCount")).toInt(), 1);
    QCOMPARE(tileFor(overview, QStringLiteral("DP-2")).value(QStringLiteral("ruleCount")).toInt(), 0);
}

void TestRuleControllerOverview::screenSizeHintNamesPinnedMonitors()
{
    // The rule editor's note under a screen-size value names the device-pixel
    // size and nearest ratio of each monitor the rule pins outside a
    // negation, and says nothing when the rule pins none.
    const QLocale previous;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const auto restoreLocale = qScopeGuard([previous]() {
        QLocale::setDefault(previous);
    });

    RuleController controller;
    QVariantMap ultrawide = screenJson(QStringLiteral("DP-1"), 5120, 1440);
    ultrawide[QStringLiteral("manufacturer")] = QStringLiteral("Samsung");
    ultrawide[QStringLiteral("model")] = QStringLiteral("Odyssey");
    const QVariantList screens{ultrawide, screenJson(QStringLiteral("DP-2"), 1080, 1920),
                               screenJson(QStringLiteral("DP-3"))};

    const QString wide = controller.screenSizeHint(
        screens,
        QVariantMap{
            {QStringLiteral("all"),
             QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-1")),
                          leafJson(QStringLiteral("screenWidth"), QStringLiteral("greaterThan"), 3000)}}});
    QVERIFY2(wide.contains(QStringLiteral("Samsung Odyssey")), qPrintable(wide));
    QVERIFY2(wide.contains(QStringLiteral("5,120 × 1,440")), qPrintable(wide));
    QVERIFY2(wide.contains(QStringLiteral("32:9")), qPrintable(wide));
    QVERIFY2(!wide.contains(QStringLiteral("DP-2")), qPrintable(wide));

    // Matched ignoring case, and a portrait panel carries the marker.
    const QString tall = controller.screenSizeHint(
        screens, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("dp-2")));
    QVERIFY2(tall.contains(QStringLiteral("9:16 (portrait)")), qPrintable(tall));

    // A negated pin, a monitor whose size is unknown, and no pin at all.
    QVERIFY(controller
                .screenSizeHint(screens,
                                QVariantMap{{QStringLiteral("none"),
                                             QVariantList{leafJson(QStringLiteral("screenId"), QStringLiteral("equals"),
                                                                   QStringLiteral("DP-1"))}}})
                .isEmpty());
    QVERIFY(controller
                .screenSizeHint(screens,
                                leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("DP-3")))
                .isEmpty());
    QVERIFY(
        controller.screenSizeHint(screens, leafJson(QStringLiteral("screenWidth"), QStringLiteral("greaterThan"), 3000))
            .isEmpty());
}

void TestRuleControllerOverview::screenSizeHintNamesVirtualScreensOfAPinnedMonitor()
{
    // A rule pinning a monitor that is split into virtual screens is judged
    // against each virtual screen's own size, so the note names each of them,
    // matched ignoring case like a whole-monitor pin. A virtual screen of
    // another monitor stays out.
    const QLocale previous;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const auto restoreLocale = qScopeGuard([previous]() {
        QLocale::setDefault(previous);
    });

    RuleController controller;
    const auto virtualScreen = [](const QString& physicalId, int index, int width, int height) {
        QVariantMap screen = screenJson(PhosphorIdentity::VirtualScreenId::make(physicalId, index), width, height);
        screen[QStringLiteral("isVirtualScreen")] = true;
        screen[QStringLiteral("virtualIndex")] = index;
        screen[QStringLiteral("connectorName")] = physicalId;
        return screen;
    };
    const QVariantList screens{virtualScreen(QStringLiteral("DP-1"), 0, 3840, 1440),
                               virtualScreen(QStringLiteral("DP-1"), 1, 1280, 1440),
                               virtualScreen(QStringLiteral("DP-2"), 0, 1920, 1080)};

    const QString hint = controller.screenSizeHint(
        screens, leafJson(QStringLiteral("screenId"), QStringLiteral("equals"), QStringLiteral("dp-1")));
    QVERIFY2(hint.contains(QStringLiteral("3,840 × 1,440")), qPrintable(hint));
    QVERIFY2(hint.contains(QStringLiteral("1,280 × 1,440")), qPrintable(hint));
    QVERIFY2(!hint.contains(QStringLiteral("1,920")), qPrintable(hint));
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

void TestRuleControllerOverview::rulesSnapshotFeedsEveryRuleRowRequiredProperty()
{
    // RuleRow binds against rulesSnapshot(), NOT against the model's roles, and
    // the snapshot is assembled key by key in rulecontroller_views.cpp. Adding a
    // model role plus a `required property` on the row is therefore NOT enough:
    // miss the snapshot line and every row is handed `undefined` for a required
    // int, which is a per-row runtime type error and an invisible feature.
    // Testing the model alone cannot see that, which is exactly how it shipped
    // once. This scrapes the QML and checks the real payload, the only place the
    // two halves meet.
    const QString qmlPath = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/rules/RuleRow.qml");
    QFile file(qmlPath);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(QStringLiteral("could not read ") + qmlPath));
    const QString src = QString::fromUtf8(file.readAll());

    // Scrape only the ROOT object's declarations. RuleRow carries an inline
    // Component for its expanded body, and a required property declared in
    // there is fed by the row itself rather than by the snapshot — attributing
    // one to the payload would fail this test with a message pointing at the
    // wrong file.
    const int inlineComponent = src.indexOf(QStringLiteral("Component {"));
    const QString rootScope = inlineComponent < 0 ? src : src.left(inlineComponent);

    // The type group is deliberately wider than \w+: a declaration such as
    // `required property list<string> x` or a qualified module type would
    // otherwise be skipped silently, which is the exact failure this test
    // exists to catch.
    static const QRegularExpression re(QStringLiteral(R"(required\s+property\s+[\w.<>]+\s+(\w+))"));
    QStringList required;
    auto it = re.globalMatch(rootScope);
    while (it.hasNext()) {
        required.append(it.next().captured(1));
    }
    // Exact, not a floor. A floor with slack still passes when drift costs one
    // declaration, which is the drift most likely to happen. Bump this together
    // with a deliberate change to RuleRow's required set.
    QCOMPARE(required.size(), qsizetype(11));

    // Page-fed properties that are NOT currently `required`, so the scrape does
    // not see them today. Listed anyway: promoting one to `required` should not
    // fail this test, because the page supplies it rather than the row payload.
    static const QStringList kFromPage{
        QStringLiteral("controller"),
        QStringLiteral("matchFieldOptions"),
        QStringLiteral("actionTypeOptions"),
        QStringLiteral("appSettings"),
    };

    // Staged the same way every other test here stages one, through the
    // controller's own authoring API. The action names a windowless event, so
    // the row's inert count must come back as 1.
    RuleController controller;
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("animation"));
    rule[QStringLiteral("actions")] =
        QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("overrideAnimationTiming")},
                                 {QStringLiteral("event"), QStringLiteral("desktop.switch")},
                                 {QStringLiteral("durationMs"), 200}}};
    QVERIFY(!controller.addRuleFromJson(rule).isEmpty());

    const QVariantList snapshot = controller.rulesSnapshot();
    QCOMPARE(snapshot.size(), 1);
    const QVariantMap entry = snapshot.first().toMap();

    QStringList missing;
    for (const QString& name : std::as_const(required)) {
        if (kFromPage.contains(name)) {
            continue;
        }
        // `ruleName` / `ruleEnabled` are the row's names for the payload's
        // `name` / `enabled`, renamed there to avoid shadowing the delegate's
        // own properties.
        QString key = name;
        if (name == QLatin1String("ruleName")) {
            key = QStringLiteral("name");
        } else if (name == QLatin1String("ruleEnabled")) {
            key = QStringLiteral("enabled");
        }
        if (!entry.contains(key)) {
            missing.append(name + QStringLiteral(" (looked for key '") + key + QStringLiteral("')"));
        }
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("RuleRow requires properties rulesSnapshot() does not provide: ")
                        + missing.join(QStringLiteral(", "))));

    // And the one this test was written for: present AND carrying a real value.
    QCOMPARE(entry.value(QStringLiteral("inertAnimationActionCount")).toInt(), 1);
}

QTEST_MAIN(TestRuleControllerOverview)

#include "test_rule_controller_overview.moc"

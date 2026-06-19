// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_rule_controller.cpp
 * @brief Coverage for WindowRuleController — the staging controller behind
 *        the unified Window Rules page.
 *
 * The controller talks to the daemon over D-Bus; in a headless unit run the
 * daemon is absent, so `daemonReachable` is false and the model starts empty.
 * The staging contract (in-memory CRUD by UUID, dirty-tracking, revert) is
 * fully exercisable without a live daemon.
 *
 * Pins:
 *   - `newEmptyRule` produces a valid, subject-shaped rule with a fresh UUID,
 *   - add / update / remove by UUID flip the dirty bit,
 *   - `monitorOverview` summarises rules per connected monitor,
 *   - `moveRule` reorders and renormalizes priorities,
 *   - the field / operator / action authoring metadata is well-formed.
 */

#include <QJSEngine>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "settings/windowrulecontroller.h"
#include "settings/windowrulemodel.h"

#include <PhosphorWindowRules/MatchExpression.h>
#include <PhosphorWindowRules/RuleAction.h>
#include <PhosphorWindowRules/WindowRule.h>

using namespace PlasmaZones;
using namespace PhosphorWindowRules;

class TestWindowRuleController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newEmptyRuleShapesBySubject();
    void addUpdateRemoveByUuid();
    void dirtyTrackingAndRevert();
    void monitorOverviewSummarises();
    void monitorOverviewIgnoresDisabledRules();
    void monitorOverviewClassifiesScrollingWithoutLayoutName();
    void monitorOverviewLayoutFollowsWinnerMode();
    void monitorOverviewIgnoresBareLayoutRules();
    void monitorOverviewLayoutFromSingleWinningRule();
    void monitorOverviewDisableEngineMatchesEffectiveMode();
    void monitorOverviewReportsLock();
    void monitorOverviewLockPriorityResolution();
    void engineModePickerExposesAllVocabularyTokens();
    void userAuthorableFilterHidesInternalActions();
    void moveRuleReorders();
    void authoringMetadata();
    void inputHints();
    void templatesProduceSeededRules();
    void actionTypesCarryDomain();
    void matchIsContextOnlyClassifies();
    void validationIssuesForJsonFlags();
    void defaultPayloadForSeedsParams();
    void asyncCommitAndRevertAreInvokable();
    void curveLabelResolverBridgesQmlNaming();
};

void TestWindowRuleController::newEmptyRuleShapesBySubject()
{
    WindowRuleController controller;

    const QVariantMap monitor = controller.newEmptyRule(QStringLiteral("monitor"));
    QVERIFY(!monitor.value(QStringLiteral("id")).toString().isEmpty());
    QVERIFY(monitor.contains(QStringLiteral("match")));
    // The monitor subject starts with a ScreenId leaf.
    const QVariantMap monitorMatch = monitor.value(QStringLiteral("match")).toMap();
    QCOMPARE(monitorMatch.value(QStringLiteral("field")).toString(), QStringLiteral("screenId"));

    const QVariantMap app = controller.newEmptyRule(QStringLiteral("application"));
    const QVariantMap appMatch = app.value(QStringLiteral("match")).toMap();
    QCOMPARE(appMatch.value(QStringLiteral("field")).toString(), QStringLiteral("appId"));

    const QVariantMap activity = controller.newEmptyRule(QStringLiteral("activity"));
    const QVariantMap activityMatch = activity.value(QStringLiteral("match")).toMap();
    QCOMPARE(activityMatch.value(QStringLiteral("field")).toString(), QStringLiteral("activity"));

    // Custom starts from the catch-all All{} composite.
    const QVariantMap custom = controller.newEmptyRule(QStringLiteral("custom"));
    QVERIFY(custom.value(QStringLiteral("match")).toMap().contains(QStringLiteral("all")));

    // Each fresh rule carries a distinct UUID.
    QVERIFY(monitor.value(QStringLiteral("id")).toString() != app.value(QStringLiteral("id")).toString());
}

void TestWindowRuleController::addUpdateRemoveByUuid()
{
    WindowRuleController controller;

    // Build a monitor rule, give it a usable action, and add it.
    QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
    rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());
    QCOMPARE(controller.model()->rowCount(), 1);

    // Update by UUID — rename it.
    QVariantMap fetched = controller.ruleJson(id);
    QCOMPARE(fetched.value(QStringLiteral("id")).toString(), id);
    fetched[QStringLiteral("name")] = QStringLiteral("Renamed");
    QVERIFY(controller.updateRuleFromJson(fetched));
    QCOMPARE(controller.ruleJson(id).value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));

    // setRuleEnabled toggles the flag.
    QVERIFY(controller.setRuleEnabled(id, false));
    QCOMPARE(controller.ruleJson(id).value(QStringLiteral("enabled")).toBool(), false);

    // Remove by UUID.
    QVERIFY(controller.removeRule(id));
    QCOMPARE(controller.model()->rowCount(), 0);
    QVERIFY(!controller.removeRule(id));
}

void TestWindowRuleController::dirtyTrackingAndRevert()
{
    WindowRuleController controller;
    QVERIFY(!controller.isDirty());

    QSignalSpy dirtySpy(&controller, &WindowRuleController::dirtyChanged);

    QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
    rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
    const QString id = controller.addRuleFromJson(rule);
    QVERIFY(!id.isEmpty());

    // Adding a rule flips the dirty bit.
    QVERIFY(controller.isDirty());
    QVERIFY(controller.hasPendingChanges());
    QVERIFY(dirtySpy.count() >= 1);

    // revert() re-fetches the daemon's authoritative set asynchronously and
    // only clears the dirty bit if the re-fetch succeeded. The contract this
    // test guards is the linkage between the async outcome and the dirty-state
    // transition: a successful revert (rulesLoaded fires) MUST clear dirty, a
    // failed revert MUST preserve it. The earlier bug was a failed revert
    // silently dropping staged edits while reporting success. The check is
    // symmetric so it passes both in a fully headless run (daemon absent →
    // revert fails → dirty stays) and on a dev machine with a live daemon
    // (revert succeeds → dirty clears).
    QSignalSpy loadedSpy(&controller, &WindowRuleController::rulesLoaded);
    controller.revert();
    // Pump the event loop briefly so the QDBusPendingCall reply (success or
    // error) lands. A timeout fall-through is acceptable — that's the
    // daemon-absent path and dirty must stay set.
    loadedSpy.wait(500);
    const bool reverted = loadedSpy.count() > 0;
    QCOMPARE(controller.isDirty(), !reverted);
}

void TestWindowRuleController::monitorOverviewSummarises()
{
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewReportsLock()
{
    // A LockContext rule pinning a monitor surfaces `locked: true` on its tile
    // (drives the lock badge), independent of any layout assignment — a
    // lock-only rule carries no SetEngineMode, so the tile has no layoutName,
    // yet it is locked. A rule whose lock value is false reports
    // `locked: false`, proving the tile reads the action's value (not mere
    // presence), mirroring resolveContextLocked.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewLockPriorityResolution()
{
    // When two opposing LockContext rules pin the SAME monitor, the tile must
    // report the HIGHEST-PRIORITY rule's value (first-wins), not last-wins and
    // not mere presence — mirroring resolveContextLocked's single-winner Locked
    // slot (cf. testContextLock_priorityResolution at the registry level).
    // addRuleFromJson appends and renormalizePriorities makes the earlier-added
    // rule the higher-priority one within the Context band, so the FIRST rule
    // added for a screen is its winner. Run both value directions so a
    // "true-always-wins" / "false-always-wins" bug fails one of the two.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewIgnoresDisabledRules()
{
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewClassifiesScrollingWithoutLayoutName()
{
    // Pin that a Scrolling-mode rule, even when carrying a stale snapping
    // layout in its action payload, produces an EMPTY `layoutName` on the
    // overview tile. The pre-Pass-3 inline `== "autotile"` / `== "snapping"`
    // classifier silently coerced Scrolling rules into the "no engine pin
    // → prefer snapping layout" fallback, mis-labelling the tile with the
    // leftover layout. A regression to that shape is caught here.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewLayoutFollowsWinnerMode()
{
    // The per-screen assignment winner (the rule carrying a SetEngineMode
    // action) supplies the engine mode AND both layout tokens; the tile shows
    // the token matching the winner's mode — mirroring the daemon's
    // resolveContextAssignment + entryFromRuleMatchActions (the AssignmentEntry
    // keeps both layouts, the active mode picks). The same rule shows its
    // snapping layout under Snapping and its algorithm under Autotile.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewIgnoresBareLayoutRules()
{
    // A layout rule with NO SetEngineMode action is never the assignment winner
    // (the daemon's resolveContextAssignment filters to hasEngineModeAction), so
    // the daemon never applies its layout — and neither does the tile. The rule
    // still counts toward ruleCount/assigned (it targets the monitor), but
    // contributes no layout label, for both a bare tiling and a bare snapping rule.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewLayoutFromSingleWinningRule()
{
    // Engine mode and layout must come from the SAME winning rule. An
    // engine-mode-only rule and a separate layout-only rule on one screen: the
    // engine rule is the assignment winner (only it has a SetEngineMode action),
    // and it carries no layout, so the tile shows NO layout — the other rule's
    // snapping layout never composes in (the daemon takes the whole entry from
    // the one winner). Both rules still count. The pre-fix independent-slot model
    // would have shown "grid" here.
    WindowRuleController controller;

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

void TestWindowRuleController::monitorOverviewDisableEngineMatchesEffectiveMode()
{
    // Pin that `tilingEnabled` on the overview tile resolves the
    // DisableEngine action against the screen's EFFECTIVE engine mode,
    // not "any DisableEngine action present". A DisableEngine{snapping}
    // rule on an Autotile-effective screen must NOT flip tilingEnabled
    // off — the cascade resolution in the daemon would never treat that
    // rule as disabling autotile. The matching positive case
    // (DisableEngine{mode} == effective mode) must flip it off.
    WindowRuleController controller;
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

void TestWindowRuleController::engineModePickerExposesAllVocabularyTokens()
{
    // Pin that the SetEngineMode + DisableEngine pickers expose exactly
    // three options — snapping / autotile / scrolling — with non-empty
    // localised labels. A regression that dropped the Scrolling enum
    // option from `engineModeOptions()` or the GPL settings-layer label
    // map would surface here.
    WindowRuleController controller;
    const QVariantList types = controller.actionTypes();
    // Each entry in `actionTypes()` carries its own `params` list (see the
    // descriptor docstring in windowrulecontroller.h:281-291). For each
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

void TestWindowRuleController::userAuthorableFilterHidesInternalActions()
{
    // Pin that the controller's actionTypes() picker honours the
    // `userAuthorable=false` flag on ActionDescriptor. Without this test the
    // filter is dead code — every shipped descriptor currently defaults to
    // userAuthorable=true, so a regression that bypasses the filter (e.g.
    // re-introducing a hand-maintained allow-list) would slip through CI.
    //
    // Register a sentinel descriptor flagged as non-authorable, walk the
    // picker, then restore the descriptor to its prior state so the rest
    // of the test suite isn't disturbed.
    using PhosphorWindowRules::ActionDescriptor;
    using PhosphorWindowRules::ActionDomain;
    using PhosphorWindowRules::ActionRegistry;

    static const QString kSentinelType = QStringLiteral("test-sentinel-internal-action");
    auto& registry = ActionRegistry::instance();
    const bool prevExists = registry.isRegistered(kSentinelType);
    const std::optional<ActionDescriptor> prev = registry.descriptor(kSentinelType);

    // RAII cleanup: restore the prior descriptor (or unregister the sentinel
    // entirely) even if an assertion throws / fails mid-test. Without this,
    // a QVERIFY2 failure between the two registerAction calls would skip
    // the trailing cleanup and leak the sentinel into the registry for the
    // remainder of the test binary's lifetime.
    struct RegistryGuard
    {
        ActionRegistry& registry;
        QString type;
        bool prevExists;
        std::optional<ActionDescriptor> prev;
        ~RegistryGuard()
        {
            if (prevExists && prev.has_value()) {
                registry.registerAction(*prev);
            } else {
                registry.unregisterAction(type);
            }
        }
    };
    RegistryGuard guard{registry, kSentinelType, prevExists, prev};

    ActionDescriptor sentinel;
    sentinel.type = kSentinelType;
    sentinel.slotFor = [](const QJsonObject&) {
        return QStringLiteral("test-sentinel-slot");
    };
    sentinel.validate = [](const QJsonObject&) {
        return true;
    };
    sentinel.terminal = false;
    sentinel.domain = ActionDomain::Window;
    sentinel.userAuthorable = false;
    registry.registerAction(sentinel);

    WindowRuleController controller;
    const QVariantList types = controller.actionTypes();
    bool found = false;
    for (const QVariant& t : types) {
        const QVariantMap tm = t.toMap();
        if (tm.value(QStringLiteral("value")).toString() == kSentinelType) {
            found = true;
            break;
        }
    }
    QVERIFY2(!found, "actionTypes() must exclude descriptors with userAuthorable=false");

    // Now flip the descriptor to userAuthorable=true and confirm the same
    // sentinel surfaces — the filter is the only thing keeping it hidden.
    sentinel.userAuthorable = true;
    registry.registerAction(sentinel);
    const QVariantList typesAuthorable = controller.actionTypes();
    bool foundAuthorable = false;
    for (const QVariant& t : typesAuthorable) {
        const QVariantMap tm = t.toMap();
        if (tm.value(QStringLiteral("value")).toString() == kSentinelType) {
            foundAuthorable = true;
            break;
        }
    }
    QVERIFY2(foundAuthorable, "actionTypes() must include descriptors with userAuthorable=true");
    // RegistryGuard's dtor handles cleanup.
}

void TestWindowRuleController::moveRuleReorders()
{
    WindowRuleController controller;

    auto makeApp = [&](const QString& appId) {
        QVariantMap rule = controller.newEmptyRule(QStringLiteral("application"));
        QVariantMap match = rule.value(QStringLiteral("match")).toMap();
        match[QStringLiteral("value")] = appId;
        rule[QStringLiteral("match")] = match;
        rule[QStringLiteral("actions")] = QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("float")}}};
        return controller.addRuleFromJson(rule);
    };

    const QString a = makeApp(QStringLiteral("a"));
    const QString b = makeApp(QStringLiteral("b"));
    const QString c = makeApp(QStringLiteral("c"));
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    // Move C before A — order becomes C, A, B.
    QVERIFY(controller.moveRule(c, a));
    WindowRuleModel* model = controller.model();
    QCOMPARE(model->index(0, 0).data(WindowRuleModel::IdRole).toString(), c);
    QCOMPARE(model->index(1, 0).data(WindowRuleModel::IdRole).toString(), a);
    QCOMPARE(model->index(2, 0).data(WindowRuleModel::IdRole).toString(), b);

    // moveRule renormalizes priorities — earlier list index ⇒ higher priority.
    const int prioFirst = model->index(0, 0).data(WindowRuleModel::PriorityRole).toInt();
    const int prioLast = model->index(2, 0).data(WindowRuleModel::PriorityRole).toInt();
    QVERIFY(prioFirst > prioLast);
}

void TestWindowRuleController::authoringMetadata()
{
    WindowRuleController controller;

    const QVariantList fields = controller.matchFields();
    QVERIFY(!fields.isEmpty());
    // Every field entry carries value / label / valueKind. The `screen` and
    // `activity` kinds drive the dedicated picker editors in QML — assert
    // at least one of each is present so a regression that reverts those
    // fields back to `string` (silently breaking the picker UX) is caught.
    bool sawScreenKind = false;
    bool sawActivityKind = false;
    bool sawWindowTypeKind = false;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(f.contains(QStringLiteral("value")));
        QVERIFY(!f.value(QStringLiteral("label")).toString().isEmpty());
        const QString kind = f.value(QStringLiteral("valueKind")).toString();
        QVERIFY(kind == QLatin1String("string") || kind == QLatin1String("number") || kind == QLatin1String("bool")
                || kind == QLatin1String("screen") || kind == QLatin1String("activity")
                || kind == QLatin1String("windowType"));
        if (kind == QLatin1String("screen")) {
            sawScreenKind = true;
        }
        if (kind == QLatin1String("activity")) {
            sawActivityKind = true;
        }
        if (kind == QLatin1String("windowType")) {
            sawWindowTypeKind = true;
            // windowType must carry an `options` array of {value, wire, label}
            // triples so the editor can render the enum dropdown.
            const QVariantList options = f.value(QStringLiteral("options")).toList();
            QVERIFY2(!options.isEmpty(), "windowType valueKind must expose enum options for the dropdown");
            for (const QVariant& opt : options) {
                const QVariantMap m = opt.toMap();
                QVERIFY(m.contains(QStringLiteral("value")));
                QVERIFY(m.contains(QStringLiteral("wire")));
                QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
            }
        }
    }
    QVERIFY(sawScreenKind);
    QVERIFY(sawActivityKind);
    QVERIFY(sawWindowTypeKind);

    // Picker categories drive the fly-out submenu grouping. Every field carries
    // a non-empty category label + a categoryOrder int. The Field enum
    // interleaves state/context, so assert grouping is by CATEGORY (via the
    // language-independent order), not by enum position. The (formerly single,
    // 19-entry) State bucket is split into fine-grained categories:
    // Identity=0, Type=1, State=2, Taskbar & switcher=3, Tiling=4, Size=5,
    // Context=6.
    QHash<QString, int> fieldCategoryOrder;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        QVERIFY(!f.value(QStringLiteral("category")).toString().isEmpty());
        QVERIFY(f.contains(QStringLiteral("categoryOrder")));
        // Every field carries one-line help (the leaf editor's info-icon
        // tooltip) — a missing description would render the icon mute again.
        QVERIFY2(
            !f.value(QStringLiteral("description")).toString().isEmpty(),
            qPrintable(QStringLiteral("field %1 has no description").arg(f.value(QStringLiteral("wire")).toString())));
        fieldCategoryOrder.insert(f.value(QStringLiteral("wire")).toString(),
                                  f.value(QStringLiteral("categoryOrder")).toInt());
    }
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("appId")), 0); // Identity
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("windowType")), 1); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isTransient")), 1); // Type
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFullscreen")), 2); // State
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isMaximized")), 2); // State (after Context in enum order)
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipTaskbar")), 3); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("skipSwitcher")), 3); // Taskbar & switcher
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("isFloating")), 4); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("zone")), 4); // Tiling
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("width")), 5); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("height")), 5); // Size
    QCOMPARE(fieldCategoryOrder.value(QStringLiteral("screenId")), 6); // Context

    // The four match conditions (IsTransient/IsNotification/Width/Height) must be
    // authorable: present in the picker with the correct value kind, and with
    // the operators their category implies (bool -> Equals only; numeric ->
    // Equals/GreaterThan/LessThan). Guards the category-driven editor wiring
    // against a future deny-set or classifier regression.
    QHash<QString, QString> kindByWire;
    QHash<QString, int> valueByWire;
    for (const QVariant& v : fields) {
        const QVariantMap f = v.toMap();
        const QString wire = f.value(QStringLiteral("wire")).toString();
        kindByWire.insert(wire, f.value(QStringLiteral("valueKind")).toString());
        valueByWire.insert(wire, f.value(QStringLiteral("value")).toInt());
    }
    QCOMPARE(kindByWire.value(QStringLiteral("isTransient")), QStringLiteral("bool"));
    QCOMPARE(kindByWire.value(QStringLiteral("isNotification")), QStringLiteral("bool"));
    QCOMPARE(kindByWire.value(QStringLiteral("width")), QStringLiteral("number"));
    QCOMPARE(kindByWire.value(QStringLiteral("height")), QStringLiteral("number"));

    const auto opWires = [&](const QString& wire) {
        QSet<QString> s;
        for (const QVariant& v : controller.operatorsForField(valueByWire.value(wire))) {
            s.insert(v.toMap().value(QStringLiteral("wire")).toString());
        }
        return s;
    };
    const QSet<QString> widthOps = opWires(QStringLiteral("width"));
    QVERIFY(widthOps.contains(QStringLiteral("lessThan")));
    QVERIFY(widthOps.contains(QStringLiteral("greaterThan")));
    QVERIFY(widthOps.contains(QStringLiteral("equals")));
    QCOMPARE(opWires(QStringLiteral("isTransient")), QSet<QString>{QStringLiteral("equals")});

    // AppId (Field enum 0) supports the AppIdMatches operator.
    const QVariantList appOps = controller.operatorsForField(0);
    QVERIFY(!appOps.isEmpty());

    // allOperators() surfaces the FULL operator vocabulary (not a field
    // subset). The leaf editor sizes the operator dropdown to the widest
    // allOperators() label so the operator column lines up across condition
    // rows — that sizing is only correct if allOperators() is a SUPERSET of
    // every field's operator set (otherwise a field operator wider than any
    // measured label would size the column too narrow and elide). Assert the
    // {value, wire, label} shape with non-empty labels and the superset
    // relationship so a regression that drops an operator is caught.
    const QVariantList allOps = controller.allOperators();
    QVERIFY(!allOps.isEmpty());
    QSet<QString> allOperatorWires;
    for (const QVariant& v : allOps) {
        const QVariantMap m = v.toMap();
        QVERIFY(m.contains(QStringLiteral("value")));
        QVERIFY(!m.value(QStringLiteral("wire")).toString().isEmpty());
        QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
        allOperatorWires.insert(m.value(QStringLiteral("wire")).toString());
    }
    for (const QVariant& v : appOps) {
        QVERIFY2(allOperatorWires.contains(v.toMap().value(QStringLiteral("wire")).toString()),
                 "operatorsForField returned an operator absent from allOperators()");
    }

    const QVariantList actions = controller.actionTypes();
    QVERIFY(!actions.isEmpty());
    bool sawFloat = false;
    // Every action carries a picker category; collect the order per wire so the
    // grouping can be spot-checked (Layout & engine=0, Gaps=1, Window=2,
    // Appearance=3, Animation=4).
    QHash<QString, int> actionCategoryOrder;
    for (const QVariant& v : actions) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("value")).toString() == QLatin1String("float"))
            sawFloat = true;
        QVERIFY(!a.value(QStringLiteral("category")).toString().isEmpty());
        QVERIFY(a.contains(QStringLiteral("categoryOrder")));
        actionCategoryOrder.insert(a.value(QStringLiteral("value")).toString(),
                                   a.value(QStringLiteral("categoryOrder")).toInt());
    }
    QVERIFY(sawFloat);
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setEngineMode")), 0); // Layout & engine
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setZonePadding")), 1); // Gaps
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("exclude")), 2); // Window
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("setOpacity")), 3); // Appearance
    QCOMPARE(actionCategoryOrder.value(QStringLiteral("excludeAnimations")), 4); // Animation
}

void TestWindowRuleController::inputHints()
{
    WindowRuleController controller;

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

void TestWindowRuleController::templatesProduceSeededRules()
{
    WindowRuleController controller;

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

    // An unknown id must return an empty map — the AddRuleSheet would
    // otherwise commit a UUID-less rule on a typo in the template id.
    const QVariantMap bogus = controller.newRuleFromTemplate(QStringLiteral("nonexistentTemplate"));
    QVERIFY(bogus.isEmpty());
}

void TestWindowRuleController::actionTypesCarryDomain()
{
    // The action row keys off this field to flag context-domain actions when
    // the match references window-property leaves — a regression that drops
    // the domain would silently lose the warning for the silently-never-fires
    // combination.
    WindowRuleController controller;
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

void TestWindowRuleController::matchIsContextOnlyClassifies()
{
    WindowRuleController controller;

    // Empty / catch-all match — context-only by definition (no leaves).
    QVERIFY(controller.matchIsContextOnly(QVariantMap{}));

    QVariantMap allEmpty;
    allEmpty[QStringLiteral("all")] = QVariantList{};
    QVERIFY(controller.matchIsContextOnly(allEmpty));

    // Single context leaf — context-only.
    QVariantMap screenLeaf;
    screenLeaf[QStringLiteral("field")] = QStringLiteral("screenId");
    screenLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    screenLeaf[QStringLiteral("value")] = QStringLiteral("DP-1");
    QVERIFY(controller.matchIsContextOnly(screenLeaf));

    // Single window leaf — NOT context-only.
    QVariantMap appLeaf;
    appLeaf[QStringLiteral("field")] = QStringLiteral("appId");
    appLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    appLeaf[QStringLiteral("value")] = QStringLiteral("firefox");
    QVERIFY(!controller.matchIsContextOnly(appLeaf));

    // An All{} carrying a window leaf — NOT context-only.
    QVariantMap mixedAll;
    QVariantList children;
    children.append(screenLeaf);
    children.append(appLeaf);
    mixedAll[QStringLiteral("all")] = children;
    QVERIFY(!controller.matchIsContextOnly(mixedAll));
}

void TestWindowRuleController::validationIssuesForJsonFlags()
{
    WindowRuleController controller;

    // Clean rule: window match + Float action → no issues.
    QVariantMap clean = controller.newEmptyRule(QStringLiteral("application"));
    QVariantMap appLeaf;
    appLeaf[QStringLiteral("field")] = QStringLiteral("appId");
    appLeaf[QStringLiteral("op")] = QStringLiteral("equals");
    appLeaf[QStringLiteral("value")] = QStringLiteral("firefox");
    clean[QStringLiteral("match")] = appLeaf;
    QVariantList cleanActions;
    QVariantMap floatAction;
    floatAction[QStringLiteral("type")] = QStringLiteral("float");
    cleanActions.append(floatAction);
    clean[QStringLiteral("actions")] = cleanActions;
    QCOMPARE(controller.validationIssuesForJson(clean).size(), 0);

    // Bad rule: same window match + SetEngineMode action → one issue at
    // index 0, pointing at the offending action.
    QVariantMap bad = clean;
    QVariantList badActions;
    QVariantMap engine;
    engine[QStringLiteral("type")] = QStringLiteral("setEngineMode");
    engine[QStringLiteral("mode")] = QStringLiteral("autotile");
    badActions.append(engine);
    bad[QStringLiteral("actions")] = badActions;
    const QVariantList issues = controller.validationIssuesForJson(bad);
    QCOMPARE(issues.size(), 1);
    const QVariantMap issue = issues.first().toMap();
    QCOMPARE(issue.value(QStringLiteral("actionIndex")).toInt(), 0);
    QCOMPARE(issue.value(QStringLiteral("actionType")).toString(), QStringLiteral("setEngineMode"));
    QVERIFY(!issue.value(QStringLiteral("message")).toString().isEmpty());

    // Partial rule (no actions yet) → zero issues; the editor only flags
    // once the user has picked an action.
    QVariantMap partial = clean;
    partial[QStringLiteral("actions")] = QVariantList{};
    QCOMPARE(controller.validationIssuesForJson(partial).size(), 0);
}

void TestWindowRuleController::defaultPayloadForSeedsParams()
{
    // The QML action row uses `defaultPayloadFor` when the user switches the
    // type picker to a new action — a stale regression that returned a bare
    // `{type: X}` map would leave SpinBoxes anchored at 0 and `canSave`
    // would gate the rule on params the user never had a chance to fill.
    WindowRuleController controller;

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

    // SetBorderColor's `value` is a `color` kind — seeded with a fully opaque
    // ARGB string (`#AARRGGBB`, alpha-first) so the picker can edit transparency
    // and the seed round-trips through QColor::HexArgb like global border colours.
    const QVariantMap borderColorPayload = controller.defaultPayloadFor(QStringLiteral("setBorderColor"));
    QCOMPARE(borderColorPayload.value(QStringLiteral("type")).toString(), QStringLiteral("setBorderColor"));
    QCOMPARE(borderColorPayload.value(QStringLiteral("value")).toString(), QStringLiteral("#FF3DAEE9"));

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

void TestWindowRuleController::asyncCommitAndRevertAreInvokable()
{
    // Pin the QML-facing commit contract: asyncCommit(bool) is the
    // escape hatch the daemonChangedWhileDirty banner uses, and
    // revert() backs its "Discard and reload" action. Both must
    // stay Q_INVOKABLE or the banner breaks at runtime.
    WindowRuleController controller;
    const QMetaObject* mo = controller.metaObject();
    QVERIFY2(mo->indexOfMethod("asyncCommit(bool)") >= 0,
             "WindowRuleController::asyncCommit must remain Q_INVOKABLE — QML's daemon-changed banner depends on it");
    QVERIFY2(mo->indexOfMethod("revert()") >= 0,
             "WindowRuleController::revert must remain Q_INVOKABLE — the daemon-changed banner's "
             "'Discard and reload' action calls it directly from QML");
}

void TestWindowRuleController::curveLabelResolverBridgesQmlNaming()
{
    // The rule-list summary resolves OverrideAnimationCurve wire strings to
    // friendly names through a QML-supplied JS resolver (CurvePresets.curveLabel
    // in production). Exercise the actual QJSValue bridge end-to-end: install a
    // real engine-backed resolver and confirm the summary renders its output,
    // and that a non-callable value clears the resolver back to the raw value.
    WindowRuleController controller;

    WindowRule curveRule;
    curveRule.id = QUuid::createUuid();
    curveRule.priority = 100;
    curveRule.match = MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox"));
    RuleAction curve;
    curve.type = QString(ActionType::OverrideAnimationCurve);
    curve.params.insert(ActionParam::Curve, QStringLiteral("0.33,1.00,0.68,1.00"));
    curveRule.actions = {curve};
    controller.model()->setRules({curveRule});

    const auto summary = [&]() {
        return controller.model()->data(controller.model()->index(0, 0), WindowRuleModel::ActionSummaryRole).toString();
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
    controller.setCurveLabelResolver(QJSValue());
    QCOMPARE(summary(), QStringLiteral("Curve: 0.33,1.00,0.68,1.00"));
}

QTEST_MAIN(TestWindowRuleController)

#include "test_window_rule_controller.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PaneRules: the shell's bundled window rule for the control-center pane.
// The rule must validate against the action registry (or the daemon's
// addRule refuses it silently at startup), match exactly the pane's app id,
// carry the per-mode placement A2 §4.2 asks for where the vocabulary can
// express it, and have a stable id so a second startup finds it present.

#include "shell/PaneRules.h"

#include <PhosphorRules/ActionParams.h>
#include <PhosphorRules/ActionTypes.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/WindowQuery.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace PhosphorRules;
using namespace PhosphorShellApp;

namespace {
const QString kAppId = QStringLiteral("org.phosphor.shell.pane.control-center");
}

class TestPaneRules : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ruleIdIsStableAndPerAppId();
    void controlCenterRuleIsValidAndManaged();
    void controlCenterRuleMatchesOnlyThePane();
    void controlCenterRuleCarriesThePerModePlacement();
    void roundTripsThroughJson();
};

void TestPaneRules::ruleIdIsStableAndPerAppId()
{
    QCOMPARE(PaneRules::ruleIdFor(kAppId), PaneRules::ruleIdFor(kAppId));
    QVERIFY(PaneRules::ruleIdFor(kAppId) != PaneRules::ruleIdFor(QStringLiteral("org.phosphor.shell.pane.calendar")));
    QVERIFY(!PaneRules::ruleIdFor(kAppId).isNull());
}

void TestPaneRules::controlCenterRuleIsValidAndManaged()
{
    const Rule rule = PaneRules::controlCenterRule(kAppId);
    QVERIFY2(rule.isValid(), "the daemon would refuse an invalid rule at addRule");
    QVERIFY(rule.validationIssues().isEmpty());
    QVERIFY(rule.managed);
    QVERIFY(rule.enabled);
    QCOMPARE(rule.id, PaneRules::ruleIdFor(kAppId));
    QVERIFY(!rule.name.isEmpty());
}

void TestPaneRules::controlCenterRuleMatchesOnlyThePane()
{
    const Rule rule = PaneRules::controlCenterRule(kAppId);
    WindowQuery pane;
    pane.appId = kAppId;
    QVERIFY(rule.match.evaluate(pane));

    WindowQuery other;
    other.appId = QStringLiteral("org.kde.konsole");
    QVERIFY(!rule.match.evaluate(other));

    // A sibling pane does not inherit the control center's placement.
    WindowQuery sibling;
    sibling.appId = QStringLiteral("org.phosphor.shell.pane.control-center-2");
    QVERIFY(!rule.match.evaluate(sibling));
}

void TestPaneRules::controlCenterRuleCarriesThePerModePlacement()
{
    const Rule rule = PaneRules::controlCenterRule(kAppId);
    QHash<QString, QJsonObject> byType;
    for (const RuleAction& action : rule.actions) {
        byType.insert(action.type, action.params);
    }
    // Snapping: zone 1, the fixed stand-in for "nearest the chip".
    QVERIFY(byType.contains(QString(ActionType::SnapToZone)));
    QCOMPARE(byType.value(QString(ActionType::SnapToZone)).value(ActionParam::Zones).toArray(), QJsonArray{1});
    // Scrolling: the 1/3 preset, in its own column.
    QVERIFY(byType.contains(QString(ActionType::OpenColumnWidth)));
    QVERIFY(qFuzzyCompare(byType.value(QString(ActionType::OpenColumnWidth)).value(ActionParam::Value).toDouble(),
                          1.0 / 3.0));
    QCOMPARE(byType.value(QString(ActionType::OpenColumnPlacement)).value(ActionParam::Value).toString(),
             QString(ColumnPlacementToken::NewColumn));
    // Tiling has no per-window insert-position action (it is a context
    // slot), so the rule must not pretend to carry one.
    QVERIFY(!byType.contains(QString(ActionType::SetInsertPosition)));
}

void TestPaneRules::roundTripsThroughJson()
{
    const Rule rule = PaneRules::controlCenterRule(kAppId);
    const auto back = Rule::fromJson(rule.toJson());
    QVERIFY(back.has_value());
    QCOMPARE(*back, rule);
}

QTEST_GUILESS_MAIN(TestPaneRules)
#include "test_pane_rules.moc"

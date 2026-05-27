// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_rule_model.cpp
 * @brief Coverage for WindowRuleModel — the single flat model behind the
 *        unified Window Rules page.
 *
 * Pins:
 *   - role exposure (id / name / enabled / summaries / counts),
 *   - the C++-derived SectionRole bucketing, including "graduation" of a
 *     composite rule to Advanced,
 *   - CRUD by UUID (add / update / remove) and reorder,
 *   - the no-op update contract (updating to an identical rule does not churn).
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>

#include "settings/windowrulemodel.h"

using namespace PlasmaZones;
using namespace PhosphorWindowRule;

namespace {

/// Build a context-only layout-assignment rule pinned to @p screenId.
WindowRule monitorRule(const QString& screenId, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 300;
    rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, screenId);
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(QLatin1String("mode"), QStringLiteral("autotile"));
    rule.actions = {engine};
    return rule;
}

/// Build a window-property float rule for @p appId.
WindowRule applicationRule(const QString& appId, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 200;
    rule.match = MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, appId);
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    rule.actions = {floatAction};
    return rule;
}

/// Build an animation-override rule.
WindowRule animationRule(const QString& windowClass, const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 100;
    rule.match = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, windowClass);
    RuleAction shader;
    shader.type = QString(ActionType::OverrideAnimationShader);
    shader.params.insert(QLatin1String("event"), QStringLiteral("window.open"));
    shader.params.insert(QLatin1String("effectId"), QStringLiteral("dissolve"));
    rule.actions = {shader};
    return rule;
}

/// Build a composite (ALL of {a leaf, a nested ANY}) rule — the kind that
/// must graduate to Advanced.
WindowRule compositeRule(const QString& name)
{
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = name;
    rule.priority = 500;
    const MatchExpression any = MatchExpression::makeAny(
        {MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("Settings")),
         MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("About"))});
    rule.match = MatchExpression::makeAll(
        {MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")), any});
    RuleAction floatAction;
    floatAction.type = QString(ActionType::Float);
    rule.actions = {floatAction};
    return rule;
}

} // namespace

class TestWindowRuleModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rolesExposed();
    void sectionDerivation();
    void compositeGraduatesToAdvanced();
    void crudByUuid();
    void updateNoOpDoesNotChurn();
    void reorder();
    void addRuleAtInsertsAtIndexWithSingleSignal();
    void validationIssueCountRole();
    void screenIdsRoleHandlesInOperator();
};

void TestWindowRuleModel::rolesExposed()
{
    WindowRuleModel model;
    const WindowRule rule = monitorRule(QStringLiteral("DP-2"), QStringLiteral("Work monitor"));
    model.setRules({rule});

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, WindowRuleModel::IdRole).toString(), rule.id.toString());
    QCOMPARE(model.data(idx, WindowRuleModel::NameRole).toString(), QStringLiteral("Work monitor"));
    QCOMPARE(model.data(idx, WindowRuleModel::EnabledRole).toBool(), true);
    QCOMPARE(model.data(idx, WindowRuleModel::ConditionCountRole).toInt(), 1);
    QCOMPARE(model.data(idx, WindowRuleModel::ActionCountRole).toInt(), 1);
    QVERIFY(!model.data(idx, WindowRuleModel::MatchSummaryRole).toString().isEmpty());
    QVERIFY(!model.data(idx, WindowRuleModel::ActionSummaryRole).toString().isEmpty());
}

void TestWindowRuleModel::sectionDerivation()
{
    QCOMPARE(WindowRuleModel::sectionFor(monitorRule(QStringLiteral("DP-1"), QString())),
             WindowRuleModel::Section::Monitor);
    QCOMPARE(WindowRuleModel::sectionFor(applicationRule(QStringLiteral("firefox"), QString())),
             WindowRuleModel::Section::Application);
    QCOMPARE(WindowRuleModel::sectionFor(animationRule(QStringLiteral("firefox"), QString())),
             WindowRuleModel::Section::Animation);

    // An Activity-pinned context rule (no monitor leaf) reads as Activity.
    WindowRule activity;
    activity.id = QUuid::createUuid();
    activity.match = MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("{uuid}"));
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(QLatin1String("mode"), QStringLiteral("snapping"));
    activity.actions = {engine};
    QCOMPARE(WindowRuleModel::sectionFor(activity), WindowRuleModel::Section::Activity);
}

void TestWindowRuleModel::compositeGraduatesToAdvanced()
{
    const WindowRule composite = compositeRule(QStringLiteral("VS Code dialogs"));
    // A nested ANY inside an ALL cannot be edited by any specialized section.
    QCOMPARE(WindowRuleModel::sectionFor(composite), WindowRuleModel::Section::Advanced);

    WindowRuleModel model;
    model.setRules({composite});
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::IsCompositeRole).toBool(), true);
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::SectionRole).value<WindowRuleModel::Section>(),
             WindowRuleModel::Section::Advanced);
    // The whole match has three leaf predicates.
    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::ConditionCountRole).toInt(), 3);
}

void TestWindowRuleModel::crudByUuid()
{
    WindowRuleModel model;
    WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    WindowRule b = applicationRule(QStringLiteral("konsole"), QStringLiteral("B"));

    QVERIFY(model.addRule(a));
    QVERIFY(model.addRule(b));
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.contains(a.id));

    // Duplicate id is rejected.
    QVERIFY(!model.addRule(a));

    // Update by id — a real change applies.
    a.name = QStringLiteral("A renamed");
    QCOMPARE(model.updateRule(a), WindowRuleModel::UpdateResult::Applied);
    QCOMPARE(model.ruleById(a.id).name, QStringLiteral("A renamed"));

    // Update of an absent id fails.
    WindowRule ghost = monitorRule(QStringLiteral("DP-9"), QStringLiteral("Ghost"));
    QCOMPARE(model.updateRule(ghost), WindowRuleModel::UpdateResult::NotFound);

    // Remove by id.
    QVERIFY(model.removeRule(a.id));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(!model.contains(a.id));
    QVERIFY(!model.removeRule(a.id));
}

void TestWindowRuleModel::updateNoOpDoesNotChurn()
{
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    model.setRules({a});

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy sectionSpy(&model, &WindowRuleModel::ruleSectionChanged);

    // Updating to an identical rule reports Unchanged and must NOT emit
    // dataChanged or ruleSectionChanged.
    QCOMPARE(model.updateRule(a), WindowRuleModel::UpdateResult::Unchanged);
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(sectionSpy.count(), 0);
}

void TestWindowRuleModel::reorder()
{
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    const WindowRule b = monitorRule(QStringLiteral("DP-2"), QStringLiteral("B"));
    const WindowRule c = monitorRule(QStringLiteral("DP-3"), QStringLiteral("C"));
    model.setRules({a, b, c});

    // Move C before A — order becomes C, A, B.
    QVERIFY(model.moveRule(c.id, a.id));
    QCOMPARE(model.index(0, 0).data(WindowRuleModel::IdRole).toString(), c.id.toString());
    QCOMPARE(model.index(1, 0).data(WindowRuleModel::IdRole).toString(), a.id.toString());
    QCOMPARE(model.index(2, 0).data(WindowRuleModel::IdRole).toString(), b.id.toString());

    // Move A to the end (null beforeId) — order becomes C, B, A.
    QVERIFY(model.moveRule(a.id, QUuid()));
    QCOMPARE(model.index(2, 0).data(WindowRuleModel::IdRole).toString(), a.id.toString());

    // An unknown id fails.
    QVERIFY(!model.moveRule(QUuid::createUuid(), QUuid()));
}

void TestWindowRuleModel::addRuleAtInsertsAtIndexWithSingleSignal()
{
    // Pin the F#5 contract: addRuleAt inserts at the requested slot
    // with EXACTLY one rowsInserted signal (no follow-up moveRows /
    // dataChanged churn). The prior duplicateRule shape fired up to
    // four model signals per click; this test guards against that
    // regressing.
    WindowRuleModel model;
    const WindowRule a = monitorRule(QStringLiteral("DP-1"), QStringLiteral("A"));
    const WindowRule b = monitorRule(QStringLiteral("DP-2"), QStringLiteral("B"));
    const WindowRule c = monitorRule(QStringLiteral("DP-3"), QStringLiteral("C"));
    model.setRules({a, b, c});

    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy countSpy(&model, &WindowRuleModel::countChanged);

    const WindowRule mid = monitorRule(QStringLiteral("DP-4"), QStringLiteral("Mid"));
    QVERIFY(model.addRuleAt(mid, 1));

    // Order is now A, Mid, B, C — Mid lands at the requested slot.
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.index(1, 0).data(WindowRuleModel::IdRole).toString(), mid.id.toString());

    // Exactly one rowsInserted, no moveRows, no dataChanged. countChanged
    // fires once.
    QCOMPARE(insertSpy.count(), 1);
    QCOMPARE(moveSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(countSpy.count(), 1);

    // Out-of-range indices clamp without erroring.
    const WindowRule head = monitorRule(QStringLiteral("DP-5"), QStringLiteral("Head"));
    QVERIFY(model.addRuleAt(head, -10));
    QCOMPARE(model.index(0, 0).data(WindowRuleModel::IdRole).toString(), head.id.toString());

    const WindowRule tail = monitorRule(QStringLiteral("DP-6"), QStringLiteral("Tail"));
    QVERIFY(model.addRuleAt(tail, 9999));
    QCOMPARE(model.index(model.rowCount() - 1, 0).data(WindowRuleModel::IdRole).toString(), tail.id.toString());

    // Duplicate id rejected even via addRuleAt.
    QVERIFY(!model.addRuleAt(a, 0));
}

void TestWindowRuleModel::validationIssueCountRole()
{
    // A well-formed rule (window-property match + window-domain Float action)
    // reports zero issues. The monitor rule above is a context-only match +
    // context action — also zero. Then the silently-never-fires combination
    // (context-domain SetEngineMode with a window-property match) reports 1.
    WindowRule cleanWindowRule = applicationRule(QStringLiteral("firefox"), QStringLiteral("clean window rule"));
    WindowRule cleanContextRule = monitorRule(QStringLiteral("DP-1"), QStringLiteral("clean monitor rule"));

    // Construct the bad combination explicitly — same shape as the rule the
    // load-time guard warns about.
    WindowRule badRule;
    badRule.id = QUuid::createUuid();
    badRule.name = QStringLiteral("firefox autotile");
    badRule.priority = 200;
    badRule.match = MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox"));
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(QLatin1String("mode"), QStringLiteral("autotile"));
    badRule.actions = {engine};

    WindowRuleModel model;
    model.setRules({cleanWindowRule, cleanContextRule, badRule});

    QCOMPARE(model.data(model.index(0, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 0);
    QCOMPARE(model.data(model.index(2, 0), WindowRuleModel::ValidationIssueCountRole).toInt(), 1);

    // The role surfaces via its registered name so QML can bind by string.
    QVERIFY(model.roleNames().values().contains(QByteArrayLiteral("validationIssueCount")));
}

void TestWindowRuleModel::screenIdsRoleHandlesInOperator()
{
    // A rule authored as `ScreenId IN [DP-2, DP-3]` must surface both ids
    // in ScreenIdsRole — earlier code did `value.toString()` on the leaf's
    // value and dropped them silently (toString() returns empty for a list).
    // The role drives the monitor-overview filter chip and the ruleCount
    // tally per monitor, so a regression here makes In-rules invisible to
    // the page.
    WindowRule rule;
    rule.id = QUuid::createUuid();
    rule.name = QStringLiteral("DP-2/DP-3 layout");
    rule.priority = 300;
    rule.match = MatchExpression::makeLeaf(Field::ScreenId, Operator::In,
                                           QVariantList{QStringLiteral("DP-2"), QStringLiteral("DP-3")});
    RuleAction engine;
    engine.type = QString(ActionType::SetEngineMode);
    engine.params.insert(QLatin1String("mode"), QStringLiteral("autotile"));
    rule.actions = {engine};

    WindowRuleModel model;
    model.setRules({rule});

    const QStringList ids = model.data(model.index(0, 0), WindowRuleModel::ScreenIdsRole).toStringList();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(QStringLiteral("DP-2")));
    QVERIFY(ids.contains(QStringLiteral("DP-3")));

    // Static helper exposed to the controller (used by monitorOverview)
    // must walk the same path.
    const QStringList collected = WindowRuleModel::screenIdsOf(rule.match);
    QCOMPARE(collected.size(), 2);
    QVERIFY(collected.contains(QStringLiteral("DP-2")));
    QVERIFY(collected.contains(QStringLiteral("DP-3")));

    // QStringList-typed predicate values must also be iterated correctly —
    // a programmatically built leaf can carry that shape (see the
    // matchexpression.cpp evaluator's dual-shape handling).
    WindowRule rule2 = rule;
    rule2.id = QUuid::createUuid();
    rule2.match = MatchExpression::makeLeaf(
        Field::ScreenId, Operator::In,
        QVariant::fromValue(QStringList{QStringLiteral("HDMI-A-1"), QStringLiteral("eDP-1")}));
    const QStringList ids2 = WindowRuleModel::screenIdsOf(rule2.match);
    QCOMPARE(ids2.size(), 2);
    QVERIFY(ids2.contains(QStringLiteral("HDMI-A-1")));
    QVERIFY(ids2.contains(QStringLiteral("eDP-1")));
}

QTEST_MAIN(TestWindowRuleModel)

#include "test_window_rule_model.moc"

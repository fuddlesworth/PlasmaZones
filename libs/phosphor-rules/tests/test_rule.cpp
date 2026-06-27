// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QJsonArray>
#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;

class TestRule : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testValidRule()
    {
        const Rule r =
            makeRule(QStringLiteral("Firefox autotile"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        QVERIFY(r.isValid());
    }

    void testInvalidRule_nullId()
    {
        Rule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        r.id = QUuid();
        QVERIFY(!r.isValid());
    }

    void testInvalidRule_badMatch()
    {
        Rule r =
            makeRule(QStringLiteral("x"), 0,
                     MatchExpression::makeLeaf(Field::Pid, Operator::Contains, QStringLiteral("12")), {floatAction()});
        QVERIFY(!r.isValid());
    }

    void testHasTerminalAction()
    {
        const Rule excludeRule = makeRule(QStringLiteral("excl"), 0, MatchExpression{}, {excludeAction()});
        QVERIFY(excludeRule.hasTerminalAction());

        const Rule plainRule = makeRule(QStringLiteral("plain"), 0, MatchExpression{}, {floatAction()});
        QVERIFY(!plainRule.hasTerminalAction());
    }

    void testJson_roundTrip()
    {
        const Rule r =
            makeRule(QStringLiteral("Keep VS Code dialogs floating"), 720,
                     MatchExpression::makeAll({
                         MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")),
                         MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, 2),
                     }),
                     {floatAction(), setOpacity(0.95), engineMode(QStringLiteral("snapping"))});

        const auto reloaded = Rule::fromJson(r.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, r);
    }

    void testJson_idHasBraces()
    {
        const Rule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        const QString idStr = r.toJson().value(QStringLiteral("id")).toString();
        QVERIFY(idStr.startsWith(QLatin1Char('{')));
        QVERIFY(idStr.endsWith(QLatin1Char('}')));
    }

    void testJson_enabledDefaultsTrueWhenAbsent()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("name"), QStringLiteral("x"));
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QJsonArray actions;
        actions.append(floatAction().toJson());
        o.insert(QStringLiteral("actions"), actions);
        // No `enabled` key.
        const auto reloaded = Rule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QVERIFY(reloaded->enabled);
    }

    void testJson_dropsRuleWithInvalidId()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QStringLiteral("not-a-uuid"));
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QVERIFY(!Rule::fromJson(o).has_value());
    }

    void testJson_dropsRuleWithNoValidActions()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        // An action whose type is unregistered — dropped — leaving zero.
        QJsonArray actions;
        actions.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("bogusAction")}});
        o.insert(QStringLiteral("actions"), actions);
        QVERIFY(!Rule::fromJson(o).has_value());
    }

    void testJson_dropsMalformedActionButKeepsRule()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QJsonArray actions;
        actions.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("bogusAction")}});
        actions.append(floatAction().toJson()); // one valid action survives
        o.insert(QStringLiteral("actions"), actions);
        const auto reloaded = Rule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->actions.size(), 1);
    }

    // ── validationIssues() ────────────────────────────────────────────────

    void testValidationIssues_catchAllWithContextAction()
    {
        // Catch-all match + context action → no issue. The provider-default
        // rule shape (empty All{}) must stay valid for every action.
        const Rule r = makeRule(QStringLiteral("provider default"), 0, MatchExpression{},
                                {engineMode(QStringLiteral("autotile"))});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_pureContextMatchWithContextAction()
    {
        // A match referencing only context fields is compatible with every
        // context-domain action — this is the bridge-authored assignment-rule
        // shape and must stay quiet.
        const Rule r =
            makeRule(QStringLiteral("display 1"), 310,
                     MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")),
                     {engineMode(QStringLiteral("snapping")), snappingLayout(QStringLiteral("{abc}"))});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_windowMatchWithContextAction()
    {
        // The flagged combination: a context-domain action with a window-class
        // predicate. The match leaf fails on the windowless context query, so
        // the action silently never fires.
        const Rule r =
            makeRule(QStringLiteral("firefox autotile"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
        QCOMPARE(issues.first().actionIndex, 0);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetEngineMode));
        QVERIFY(!issues.first().message.isEmpty());
    }

    void testValidationIssues_mixedAllMatchWithContextAction()
    {
        // A flat All{} with both window and context leaves still contains a
        // window leaf, so the rule is non-context-only and the context action
        // is flagged. (The window leaf fails on a context query, dragging the
        // whole All down to false.)
        const Rule r =
            makeRule(QStringLiteral("firefox on display-1"), 200,
                     MatchExpression::makeAll({
                         MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                         MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")),
                     }),
                     {snappingLayout(QStringLiteral("{abc}"))});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
    }

    void testValidationIssues_windowMatchWithWindowAction()
    {
        // Window-domain actions are always compatible — the per-window
        // evaluator carries every field, so the match never silently fails.
        const Rule r =
            makeRule(QStringLiteral("firefox float"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {floatAction(), setOpacity(0.9)});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_contextMatchWithWindowAction()
    {
        // Context-only match + window action → valid. Means "float every
        // window on screen X" — unusual but a legitimate user intent.
        const Rule r = makeRule(
            QStringLiteral("float everything on display-1"), 310,
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")), {floatAction()});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_gapActionWithWindowMatch()
    {
        // Gap overrides are context-domain — pairing one with a window-property
        // match silently never fires (the gap is resolved during the windowless
        // context pass). The validator must flag it just like the engine/layout
        // context actions.
        const Rule r =
            makeRule(QStringLiteral("konsole padding"), 200,
                     MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("org.kde.konsole")),
                     {innerGap(0)});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetInnerGap));
    }

    void testValidationIssues_gapActionWithContextMatch()
    {
        // Gap override + context-only match → valid: "zero padding on activity
        // X" is exactly the intended use of a context gap rule.
        const Rule r = makeRule(
            QStringLiteral("gaming no gaps"), 510,
            MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("gaming-uuid")), {innerGap(0)});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_multipleActionsEachFlaggedIndependently()
    {
        // Two context-domain actions on a window match → two issues, each
        // pointing at its own index, so the UI can pin a marker per action.
        const Rule r =
            makeRule(QStringLiteral("firefox stuff"), 200,
                     MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile")), snappingLayout(QStringLiteral("{abc}")), floatAction()});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 2);
        QCOMPARE(issues.at(0).actionIndex, 0);
        QCOMPARE(issues.at(0).actionType, QString(ActionType::SetEngineMode));
        QCOMPARE(issues.at(1).actionIndex, 1);
        QCOMPARE(issues.at(1).actionType, QString(ActionType::SetSnappingLayout));
        // The window-domain floatAction at index 2 must not be flagged.
    }
};

QTEST_GUILESS_MAIN(TestRule)
#include "test_rule.moc"

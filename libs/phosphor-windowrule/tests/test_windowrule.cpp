// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QJsonArray>
#include <QTest>

using namespace PhosphorWindowRule;
using namespace PhosphorWindowRule::TestHelpers;

class TestWindowRule : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testValidRule()
    {
        const WindowRule r =
            makeRule(QStringLiteral("Firefox autotile"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        QVERIFY(r.isValid());
    }

    void testInvalidRule_nullId()
    {
        WindowRule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        r.id = QUuid();
        QVERIFY(!r.isValid());
    }

    void testInvalidRule_badMatch()
    {
        WindowRule r =
            makeRule(QStringLiteral("x"), 0,
                     MatchExpression::makeLeaf(Field::Pid, Operator::Contains, QStringLiteral("12")), {floatAction()});
        QVERIFY(!r.isValid());
    }

    void testHasTerminalAction()
    {
        const WindowRule excludeRule = makeRule(QStringLiteral("excl"), 0, MatchExpression{}, {excludeAction()});
        QVERIFY(excludeRule.hasTerminalAction());

        const WindowRule plainRule = makeRule(QStringLiteral("plain"), 0, MatchExpression{}, {floatAction()});
        QVERIFY(!plainRule.hasTerminalAction());
    }

    void testJson_roundTrip()
    {
        const WindowRule r =
            makeRule(QStringLiteral("Keep VS Code dialogs floating"), 720,
                     MatchExpression::makeAll({
                         MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")),
                         MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, 2),
                     }),
                     {floatAction(), setOpacity(0.95), engineMode(QStringLiteral("snapping"))});

        const auto reloaded = WindowRule::fromJson(r.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, r);
    }

    void testJson_idHasBraces()
    {
        const WindowRule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
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
        const auto reloaded = WindowRule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QVERIFY(reloaded->enabled);
    }

    void testJson_dropsRuleWithInvalidId()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QStringLiteral("not-a-uuid"));
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QVERIFY(!WindowRule::fromJson(o).has_value());
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
        QVERIFY(!WindowRule::fromJson(o).has_value());
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
        const auto reloaded = WindowRule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->actions.size(), 1);
    }
};

QTEST_MAIN(TestWindowRule)
#include "test_windowrule.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleAction.h>

#include <QJsonObject>
#include <QTest>

using namespace PhosphorWindowRule;

namespace {

RuleAction engineModeAction(const QString& mode)
{
    RuleAction a;
    a.type = QString(ActionType::SetEngineMode);
    a.params.insert(QStringLiteral("mode"), mode);
    return a;
}

} // namespace

class TestRuleAction : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testJson_roundTrip()
    {
        const RuleAction a = engineModeAction(QStringLiteral("autotile"));
        const auto reloaded = RuleAction::fromJson(a.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, a);
    }

    void testJson_typeWrittenInline()
    {
        const RuleAction a = engineModeAction(QStringLiteral("snapping"));
        const QJsonObject o = a.toJson();
        QCOMPARE(o.value(QStringLiteral("type")).toString(), QString(ActionType::SetEngineMode));
        QCOMPARE(o.value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    }

    void testJson_rejectsMissingType()
    {
        QJsonObject o;
        o.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_rejectsUnregisteredType()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("notARealAction"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_rejectsInvalidParams()
    {
        // setEngineMode requires a non-empty `mode` string.
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetEngineMode));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("mode"), QString());
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_animationActionRequiresEvent()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::OverrideAnimationShader));
        o.insert(QStringLiteral("effectId"), QStringLiteral("dissolve"));
        // No `event` — must be rejected (the slot is event-scoped).
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("event"), QStringLiteral("window.open"));
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    void testJson_opacityRange()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetOpacity));
        o.insert(QStringLiteral("value"), 1.5);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("value"), 0.95);
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    void testEquality()
    {
        QVERIFY(engineModeAction(QStringLiteral("autotile")) == engineModeAction(QStringLiteral("autotile")));
        QVERIFY(engineModeAction(QStringLiteral("autotile")) != engineModeAction(QStringLiteral("snapping")));
    }

    void testActionDomain_contextActions()
    {
        // The four context-domain actions all fill slots consumed during the
        // screen/desktop/activity resolution pass. They must report
        // ActionDomain::Context so the validator can flag mismatched matches.
        const QList<QLatin1StringView> contextTypes = {
            ActionType::SetEngineMode,
            ActionType::SetSnappingLayout,
            ActionType::SetTilingAlgorithm,
            ActionType::DisableEngine,
        };
        for (const QLatin1StringView type : contextTypes) {
            const auto descriptor = ActionRegistry::instance().descriptor(QString::fromLatin1(type));
            QVERIFY2(descriptor.has_value(), type.data());
            QCOMPARE(descriptor->domain, ActionDomain::Context);
        }
    }

    void testActionDomain_windowActions()
    {
        // Window-domain actions cover the per-window evaluation pass — exclude,
        // float, animation overrides, opacity. They run against a WindowQuery
        // that carries both window and context fields, so any match shape is
        // valid for them.
        const QList<QLatin1StringView> windowTypes = {
            ActionType::Exclude,
            ActionType::Float,
            ActionType::OverrideAnimationShader,
            ActionType::OverrideAnimationTiming,
            ActionType::OverrideAnimationCurve,
            ActionType::SetOpacity,
        };
        for (const QLatin1StringView type : windowTypes) {
            const auto descriptor = ActionRegistry::instance().descriptor(QString::fromLatin1(type));
            QVERIFY2(descriptor.has_value(), type.data());
            QCOMPARE(descriptor->domain, ActionDomain::Window);
        }
    }

    void testActionDomain_unregisteredFallsBackToWindow()
    {
        // An unregistered action type is treated as window-domain — the
        // conservative default so an unknown action gets the looser
        // compatibility check rather than being incorrectly flagged.
        RuleAction unknown;
        unknown.type = QStringLiteral("notARealAction");
        QCOMPARE(ActionRegistry::instance().domainFor(unknown), ActionDomain::Window);
    }

    void testActionDomain_allBuiltinsCovered()
    {
        // Canary — every registered built-in must be either context or window
        // domain. The cast-back asserts the enum stayed two-valued; adding a
        // third domain without updating this test would still pass the
        // compiler.
        for (const QString& type : ActionRegistry::instance().registeredTypes()) {
            const auto descriptor = ActionRegistry::instance().descriptor(type);
            QVERIFY(descriptor.has_value());
            const int d = static_cast<int>(descriptor->domain);
            QVERIFY(d == static_cast<int>(ActionDomain::Context) || d == static_cast<int>(ActionDomain::Window));
        }
    }
};

QTEST_MAIN(TestRuleAction)
#include "test_ruleaction.moc"

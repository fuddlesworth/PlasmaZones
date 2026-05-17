// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/AnimationAppRuleBridge.h>
#include <PhosphorWindowRule/RuleEvaluator.h>

#include <PhosphorAnimation/AnimationAppRule.h>

#include <QTest>

using namespace PhosphorWindowRule;
using PhosphorAnimationShaders::AnimationAppRule;
using PhosphorAnimationShaders::AnimationAppRuleList;

namespace {

AnimationAppRule shaderRule(const QString& classPattern, const QString& eventPath, const QString& effectId)
{
    AnimationAppRule r;
    r.classPattern = classPattern;
    r.eventPath = eventPath;
    r.kind = AnimationAppRule::Kind::Shader;
    r.effectId = effectId;
    return r;
}

AnimationAppRule timingRule(const QString& classPattern, const QString& eventPath, const QString& curve, int durationMs)
{
    AnimationAppRule r;
    r.classPattern = classPattern;
    r.eventPath = eventPath;
    r.kind = AnimationAppRule::Kind::Timing;
    r.curve = curve;
    r.durationMs = durationMs;
    return r;
}

WindowQuery windowQuery(const QString& windowClass)
{
    WindowQuery q;
    q.windowClass = windowClass;
    q.screenId = QStringLiteral("DP-1");
    return q;
}

QString shaderSlot(const QString& event)
{
    return QString(ActionSlot::AnimShaderPrefix) + event;
}

QString timingSlot(const QString& event)
{
    return QString(ActionSlot::AnimTimingPrefix) + event;
}

} // namespace

class TestAnimationAppRuleBridge : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Empty input ──

    void testEmptyList_yieldsEmptySet()
    {
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(AnimationAppRuleList{});
        QVERIFY(set.isEmpty());
    }

    // ── Rule shape ──

    void testShaderRule_buildsWindowClassMatchAndShaderAction()
    {
        AnimationAppRuleList list;
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        QCOMPARE(set.count(), 1);

        const WindowRule& rule = set.rules().first();
        QVERIFY(rule.match.isLeaf());
        QCOMPARE(rule.match.predicate().field, Field::WindowClass);
        QCOMPARE(rule.match.predicate().op, Operator::Contains);
        QCOMPARE(rule.match.predicate().value.toString(), QStringLiteral("firefox"));
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(rule.actions.first().type, QString(ActionType::OverrideAnimationShader));
        QCOMPARE(rule.actions.first().params.value(QLatin1String("event")).toString(), QStringLiteral("window.open"));
        QCOMPARE(rule.actions.first().params.value(QLatin1String("effectId")).toString(), QStringLiteral("dissolve"));
    }

    void testTimingRule_buildsTimingAction()
    {
        AnimationAppRuleList list;
        list.append(timingRule(QStringLiteral("konsole"), QStringLiteral("window.close"),
                               QStringLiteral("0.25,0.1,0.25,1"), 320));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        QCOMPARE(set.count(), 1);

        const WindowRule& rule = set.rules().first();
        QCOMPARE(rule.actions.first().type, QString(ActionType::OverrideAnimationTiming));
        QCOMPARE(rule.actions.first().params.value(QLatin1String("curve")).toString(),
                 QStringLiteral("0.25,0.1,0.25,1"));
        QCOMPARE(rule.actions.first().params.value(QLatin1String("durationMs")).toInt(), 320);
    }

    void testTimingRule_zeroDurationOmitsKey()
    {
        AnimationAppRuleList list;
        list.append(timingRule(QStringLiteral("konsole"), QStringLiteral("window.close"), QString(), 0));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        QCOMPARE(set.count(), 1);
        // durationMs <= 0 is the "inherit" sentinel — the key is omitted.
        QVERIFY(!set.rules().first().actions.first().params.contains(QLatin1String("durationMs")));
        QVERIFY(!set.rules().first().actions.first().params.contains(QLatin1String("curve")));
    }

    // ── Empty pattern / event are dropped ──

    void testEmptyClassPatternDropped()
    {
        AnimationAppRuleList list;
        // AnimationAppRuleList::append gates empty pattern/event, so build the
        // entry directly and convert it.
        AnimationAppRule r = shaderRule(QString(), QStringLiteral("window.open"), QStringLiteral("pop"));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet([&] {
            AnimationAppRuleList l;
            l.append(r); // rejected by the gate — list stays empty
            return l;
        }());
        QVERIFY(set.isEmpty());
    }

    // ── Event-scoped slots stay independent ──

    void testShaderAndTimingForSameEvent_fillDistinctSlots()
    {
        AnimationAppRuleList list;
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        list.append(timingRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QString(), 400));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        const ResolvedActions r = eval.resolve(windowQuery(QStringLiteral("firefox")));
        QVERIFY(r.hasSlot(shaderSlot(QStringLiteral("window.open"))));
        QVERIFY(r.hasSlot(timingSlot(QStringLiteral("window.open"))));
    }

    void testDifferentEvents_doNotCollide()
    {
        AnimationAppRuleList list;
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.close"), QStringLiteral("shrink")));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        const ResolvedActions r = eval.resolve(windowQuery(QStringLiteral("firefox")));
        QCOMPARE(r.slot(shaderSlot(QStringLiteral("window.open")))->params.value(QLatin1String("effectId")).toString(),
                 QStringLiteral("dissolve"));
        QCOMPARE(r.slot(shaderSlot(QStringLiteral("window.close")))->params.value(QLatin1String("effectId")).toString(),
                 QStringLiteral("shrink"));
    }

    // ── First-match-per-axis preserved via descending priority ──

    void testFirstMatchWinsForSameEventAxis()
    {
        AnimationAppRuleList list;
        // Both target firefox + window.open shader axis — first entry wins.
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("first")));
        list.append(shaderRule(QStringLiteral("fire"), QStringLiteral("window.open"), QStringLiteral("second")));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        const ResolvedActions r = eval.resolve(windowQuery(QStringLiteral("firefox")));
        QCOMPARE(r.slot(shaderSlot(QStringLiteral("window.open")))->params.value(QLatin1String("effectId")).toString(),
                 QStringLiteral("first"));
    }

    // ── Engaged-empty effectId sentinel ──

    void testEngagedEmptyEffectId_fillsSlotButValueIsEmpty()
    {
        AnimationAppRuleList list;
        // Empty effectId = "block the per-event default" sentinel.
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QString()));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        const ResolvedActions r = eval.resolve(windowQuery(QStringLiteral("firefox")));
        const QString slot = shaderSlot(QStringLiteral("window.open"));
        // Slot IS filled — the rule matched — but effectId is empty. This is
        // distinct from an unfilled slot ("no rule matched").
        QVERIFY(r.hasSlot(slot));
        QVERIFY(r.slot(slot).has_value());
        QVERIFY(r.slot(slot)->params.value(QLatin1String("effectId")).toString().isEmpty());
    }

    void testNoMatch_leavesSlotUnfilled()
    {
        AnimationAppRuleList list;
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        const ResolvedActions r = eval.resolve(windowQuery(QStringLiteral("konsole")));
        QVERIFY(!r.hasSlot(shaderSlot(QStringLiteral("window.open"))));
    }

    // ── hasAnyMatch — the shouldAnimateWindow re-enable check ──

    void testHasAnyMatch_eventAgnostic()
    {
        AnimationAppRuleList list;
        list.append(shaderRule(QStringLiteral("firefox"), QStringLiteral("window.open"), QStringLiteral("dissolve")));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        RuleEvaluator eval(set);

        // The class-targeted rule signals intent regardless of event.
        QVERIFY(eval.hasAnyMatch(windowQuery(QStringLiteral("firefox"))));
        QVERIFY(!eval.hasAnyMatch(windowQuery(QStringLiteral("konsole"))));
    }
};

QTEST_MAIN(TestAnimationAppRuleBridge)
#include "test_animationapprulebridge.moc"

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
        // A Timing action must not carry the Shader-only `effectId` field —
        // the two axes have disjoint param sets.
        QVERIFY(!rule.actions.first().params.contains(QLatin1String("effectId")));
    }

    void testTimingRule_zeroDurationOmitsKey()
    {
        AnimationAppRuleList list;
        list.append(timingRule(QStringLiteral("konsole"), QStringLiteral("window.close"), QString(), 0));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        QCOMPARE(set.count(), 1);
        // durationMs <= 0 is the "inherit" sentinel — the key is omitted.
        QVERIFY(!set.rules().first().actions.first().params.contains(QLatin1String("durationMs")));
        // This case has BOTH an empty curve and a zero duration, so it only
        // proves the duration gate jointly — testTimingRule_emptyCurveOmitsKey
        // isolates the curve gate.
        QVERIFY(!set.rules().first().actions.first().params.contains(QLatin1String("curve")));
    }

    void testTimingRule_emptyCurveOmitsKey()
    {
        AnimationAppRuleList list;
        // Non-zero duration with an empty curve — isolates the curve gate from
        // the duration gate. The duration key is written; the curve key is not.
        list.append(timingRule(QStringLiteral("konsole"), QStringLiteral("window.close"), QString(), 300));
        const WindowRuleSet set = AnimationAppRuleBridge::toRuleSet(list);
        QCOMPARE(set.count(), 1);
        const QJsonObject& params = set.rules().first().actions.first().params;
        QVERIFY(!params.contains(QLatin1String("curve")));
        QCOMPARE(params.value(QLatin1String("durationMs")).toInt(), 300);
    }

    // ── Empty pattern / event are dropped ──

    // The bridge's `toRuleSet` iterates `AnimationAppRuleList::entries()`, and
    // every public path that populates that list — `append`, `setEntries`,
    // `fromJson` — gates out empty `classPattern` / `eventPath` entries. The
    // bridge therefore can never observe such an entry through the supported
    // API; its own `continue` guard is defence-in-depth only. The reachable
    // contract these tests can pin is the LIST gate that makes an
    // empty-pattern (or empty-event) entry impossible to feed to the bridge in
    // the first place — pin it directly so a regression in that gate (which
    // would let "match every window" rules through) is caught here.
    void testEmptyPatternRejectedByListGate()
    {
        AnimationAppRuleList list;
        // append() returns false and leaves the list empty for an empty
        // classPattern — this is the gate the bridge relies on.
        QVERIFY(!list.append(shaderRule(QString(), QStringLiteral("window.open"), QStringLiteral("pop"))));
        QVERIFY(list.isEmpty());
        QVERIFY(AnimationAppRuleBridge::toRuleSet(list).isEmpty());
    }

    void testEmptyEventRejectedByListGate()
    {
        AnimationAppRuleList list;
        // The event gate is the other half: an empty eventPath maps to no
        // action slot, so the list rejects it before the bridge ever sees it.
        QVERIFY(!list.append(shaderRule(QStringLiteral("firefox"), QString(), QStringLiteral("pop"))));
        QVERIFY(list.isEmpty());
        QVERIFY(AnimationAppRuleBridge::toRuleSet(list).isEmpty());
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QTest>

using namespace PhosphorWindowRule;
using namespace PhosphorWindowRule::TestHelpers;

namespace {

WindowQuery konsoleQuery()
{
    WindowQuery q;
    q.appId = QStringLiteral("org.kde.konsole");
    q.windowClass = QStringLiteral("konsole");
    q.screenId = QStringLiteral("DP-2");
    q.virtualDesktop = 1;
    return q;
}

} // namespace

class TestRuleEvaluator : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Empty / no-match ──

    void testEmptyRuleSet_resolvesEmpty()
    {
        WindowRuleSet set;
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.isEmpty());
        QVERIFY(!result.isExcluded());
    }

    void testNoMatchingRule_resolvesEmpty()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("firefox only"), 100,
                             MatchExpression::makeLeaf(Field::WindowClass, Operator::Equals, QStringLiteral("firefox")),
                             {floatAction()}));
        RuleEvaluator eval(set);
        QVERIFY(eval.resolve(konsoleQuery()).isEmpty());
    }

    void testDisabledRuleIgnored()
    {
        WindowRuleSet set;
        WindowRule r = makeRule(QStringLiteral("disabled"), 100, MatchExpression{}, {floatAction()});
        r.enabled = false;
        set.addRule(r);
        RuleEvaluator eval(set);
        QVERIFY(eval.resolve(konsoleQuery()).isEmpty());
    }

    // ── Slot accumulation ──

    void testCatchAllMatches()
    {
        WindowRuleSet set;
        set.addRule(
            makeRule(QStringLiteral("catch-all"), 0, MatchExpression{}, {engineMode(QStringLiteral("snapping"))}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.hasSlot(QString(ActionSlot::EngineMode)));
    }

    void testActionsInDifferentSlotsStack()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("multi"), 100, MatchExpression{},
                             {engineMode(QStringLiteral("autotile")), floatAction(), setOpacity(0.9)}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.hasSlot(QString(ActionSlot::EngineMode)));
        QVERIFY(result.hasSlot(QString(ActionSlot::Float)));
        QVERIFY(result.hasSlot(QString(ActionSlot::Opacity)));
    }

    void testFirstRulePerSlotWins()
    {
        WindowRuleSet set;
        // Higher priority sets autotile; lower priority's snapping is ignored.
        set.addRule(makeRule(QStringLiteral("high"), 500, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        set.addRule(makeRule(QStringLiteral("low"), 100, MatchExpression{}, {engineMode(QStringLiteral("snapping"))}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        const auto action = result.slot(QString(ActionSlot::EngineMode));
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QStringLiteral("mode")).toString(), QStringLiteral("autotile"));
    }

    void testPriorityTie_brokenByListOrder()
    {
        WindowRuleSet set;
        // Same priority — the rule added first wins the slot.
        set.addRule(
            makeRule(QStringLiteral("first"), 100, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        set.addRule(
            makeRule(QStringLiteral("second"), 100, MatchExpression{}, {engineMode(QStringLiteral("snapping"))}));
        RuleEvaluator eval(set);
        const auto action = eval.resolve(konsoleQuery()).slot(QString(ActionSlot::EngineMode));
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QStringLiteral("mode")).toString(), QStringLiteral("autotile"));
    }

    // ── Slot-unfilled vs slot-filled-empty ──

    void testSlotUnfilled_isNullopt()
    {
        WindowRuleSet set;
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(!result.hasSlot(QString(ActionSlot::Float)));
        QVERIFY(!result.slot(QString(ActionSlot::Float)).has_value());
    }

    void testSlotFilledWithEmptyParams_isDistinctFromUnfilled()
    {
        // `float` carries empty params. A matched float rule must produce a
        // *present* slot — distinguishable from an unfilled slot.
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("float"), 100, MatchExpression{}, {floatAction()}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.hasSlot(QString(ActionSlot::Float)));
        const auto action = result.slot(QString(ActionSlot::Float));
        QVERIFY(action.has_value()); // present
        QVERIFY(action->params.isEmpty()); // ...but params are empty
    }

    // ── Terminal Exclude ──

    void testExcludeIsTerminal()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("exclude"), 100, MatchExpression{}, {excludeAction()}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.isExcluded());
    }

    void testExcludeStopsLowerPriorityRules()
    {
        WindowRuleSet set;
        // Exclude at high priority; a lower-priority engine-mode rule must
        // NOT contribute (the walk stops).
        set.addRule(makeRule(QStringLiteral("exclude"), 500, MatchExpression{}, {excludeAction()}));
        set.addRule(
            makeRule(QStringLiteral("engine"), 100, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.isExcluded());
        QVERIFY(!result.hasSlot(QString(ActionSlot::EngineMode)));
    }

    void testHigherPrioritySlotsKeptBeforeExclude()
    {
        WindowRuleSet set;
        // engine-mode at higher priority than exclude — it fills first.
        set.addRule(
            makeRule(QStringLiteral("engine"), 500, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        set.addRule(makeRule(QStringLiteral("exclude"), 100, MatchExpression{}, {excludeAction()}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.isExcluded());
        QVERIFY(result.hasSlot(QString(ActionSlot::EngineMode)));
    }

    // ── Animation event-scoped slots ──

    void testAnimationEventSlotsIndependent()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("open"), 100, MatchExpression{},
                             {overrideShader(QStringLiteral("window.open"), QStringLiteral("dissolve"))}));
        set.addRule(makeRule(QStringLiteral("close"), 100, MatchExpression{},
                             {overrideShader(QStringLiteral("window.close"), QStringLiteral("popout"))}));
        RuleEvaluator eval(set);
        const ResolvedActions result = eval.resolve(konsoleQuery());
        QVERIFY(result.hasSlot(QStringLiteral("anim-shader:window.open")));
        QVERIFY(result.hasSlot(QStringLiteral("anim-shader:window.close")));
    }

    // ── hasAnyMatch ──

    void testHasAnyMatch()
    {
        WindowRuleSet set;
        set.addRule(
            makeRule(QStringLiteral("konsole"), 100,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("konsole")),
                     {floatAction()}));
        RuleEvaluator eval(set);
        QVERIFY(eval.hasAnyMatch(konsoleQuery()));

        WindowQuery other;
        other.windowClass = QStringLiteral("firefox");
        other.screenId = QStringLiteral("DP-2");
        QVERIFY(!eval.hasAnyMatch(other));
    }

    // ── Cache ──

    void testResolveCached_returnsSameResult()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 100, MatchExpression{}, {floatAction()}));
        RuleEvaluator eval(set);
        const QString winId = QStringLiteral("org.kde.konsole|abc");
        const ResolvedActions first = eval.resolveCached(winId, konsoleQuery());
        const ResolvedActions second = eval.resolveCached(winId, konsoleQuery());
        QVERIFY(first == second);
        QCOMPARE(eval.cacheSize(), 1);
    }

    void testResolveCached_invalidatedByRevisionBump()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 100, MatchExpression{}, {floatAction()}));
        RuleEvaluator eval(set);
        const QString winId = QStringLiteral("org.kde.konsole|abc");

        ResolvedActions before = eval.resolveCached(winId, konsoleQuery());
        QVERIFY(before.hasSlot(QString(ActionSlot::Float)));
        QVERIFY(!before.hasSlot(QString(ActionSlot::EngineMode)));

        // Mutating the set bumps the revision — the stale cache entry must
        // be discarded on the next access.
        set.addRule(makeRule(QStringLiteral("b"), 200, MatchExpression{}, {engineMode(QStringLiteral("autotile"))}));
        ResolvedActions after = eval.resolveCached(winId, konsoleQuery());
        QVERIFY(after.hasSlot(QString(ActionSlot::EngineMode)));
    }

    void testClearCache()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 100, MatchExpression{}, {floatAction()}));
        RuleEvaluator eval(set);
        eval.resolveCached(QStringLiteral("w1"), konsoleQuery());
        eval.resolveCached(QStringLiteral("w2"), konsoleQuery());
        QCOMPARE(eval.cacheSize(), 2);
        eval.clearCache();
        QCOMPARE(eval.cacheSize(), 0);
    }
};

QTEST_MAIN(TestRuleEvaluator)
#include "test_ruleevaluator.moc"

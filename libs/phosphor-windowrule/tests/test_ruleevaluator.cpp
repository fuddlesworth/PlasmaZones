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

    // ExcludeAnimations rules authored over the new IsNotification / IsTransient
    // / Width match fields resolve to isExcluded() through the full evaluator —
    // e.g. the built-in "Don't animate small windows" template (Width < 300).
    void testExcludeAnimationsOverNewFields()
    {
        const auto excludeAnimations = []() {
            RuleAction a;
            a.type = QString(ActionType::ExcludeAnimations);
            return a;
        };
        WindowRuleSet set;
        set.setRules({
            makeRule(QStringLiteral("exclude notifications"), 0,
                     MatchExpression::makeLeaf(Field::IsNotification, Operator::Equals, true), {excludeAnimations()}),
            makeRule(QStringLiteral("exclude transient"), 0,
                     MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true), {excludeAnimations()}),
            makeRule(QStringLiteral("exclude sub-300px-wide"), 0,
                     MatchExpression::makeLeaf(Field::Width, Operator::LessThan, 300), {excludeAnimations()}),
        });
        RuleEvaluator eval(set);

        WindowQuery notif;
        notif.appId = QStringLiteral("app");
        notif.isNotification = true;
        notif.isTransient = false;
        notif.width = 800;
        QVERIFY(eval.resolve(notif).isExcluded());

        WindowQuery tiny;
        tiny.appId = QStringLiteral("app");
        tiny.isNotification = false;
        tiny.isTransient = false;
        tiny.width = 250;
        QVERIFY(eval.resolve(tiny).isExcluded());

        WindowQuery normal;
        normal.appId = QStringLiteral("app");
        normal.isNotification = false;
        normal.isTransient = false;
        normal.width = 1200;
        QVERIFY(!eval.resolve(normal).isExcluded());

        // Windowless context query — every new field is absent, so no rule
        // matches (window-property predicates are inert during context resolution).
        WindowQuery ctx;
        ctx.screenId = QStringLiteral("DP-1");
        QVERIFY(!eval.resolve(ctx).isExcluded());
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

    // The daemon's restore-position predicate reads the RestorePosition slot's
    // boolean `value` to override the global setting per window. A rule matching
    // on appId must surface that value through the evaluator.
    void testRestorePositionRuleResolvesValue()
    {
        WindowRuleSet set;
        set.addRule(
            makeRule(QStringLiteral("no-restore-konsole"), 100,
                     MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("org.kde.konsole")),
                     {restorePosition(false)}));
        RuleEvaluator eval(set);

        const ResolvedActions matched = eval.resolve(konsoleQuery());
        QVERIFY(matched.hasSlot(QString(ActionSlot::RestorePosition)));
        const std::optional<RuleAction> action = matched.slot(QString(ActionSlot::RestorePosition));
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QString(ActionParam::Value)).toBool(), false);

        // A different app does not match → slot empty, daemon falls back to the
        // global setting.
        WindowQuery firefox;
        firefox.appId = QStringLiteral("org.mozilla.firefox");
        firefox.screenId = QStringLiteral("DP-2");
        QVERIFY(!eval.resolve(firefox).hasSlot(QString(ActionSlot::RestorePosition)));
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

    void testPriorityTie_stableAcrossThreeEqualRules()
    {
        // Three equal-priority rules — an unstable sort could pass a two-rule
        // tie by luck, so prove stability with three distinct engine-mode tags.
        // The first-added rule must win.
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("alpha"), 100, MatchExpression{}, {engineMode(QStringLiteral("alpha"))}));
        set.addRule(makeRule(QStringLiteral("beta"), 100, MatchExpression{}, {engineMode(QStringLiteral("beta"))}));
        set.addRule(makeRule(QStringLiteral("gamma"), 100, MatchExpression{}, {engineMode(QStringLiteral("gamma"))}));
        RuleEvaluator eval(set);
        const auto action = eval.resolve(konsoleQuery()).slot(QString(ActionSlot::EngineMode));
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QStringLiteral("mode")).toString(), QStringLiteral("alpha"));
    }

    void testPriorityTie_reverseInsertionOrderProvesOrderIsTiebreaker()
    {
        // Same three tags inserted in the opposite order: if priority (all
        // equal) were the tiebreaker the winner would be unchanged, but list
        // order is — so "gamma" (now first-added) must win. This pins that the
        // tiebreaker is insertion order, not priority or tag identity.
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("gamma"), 100, MatchExpression{}, {engineMode(QStringLiteral("gamma"))}));
        set.addRule(makeRule(QStringLiteral("beta"), 100, MatchExpression{}, {engineMode(QStringLiteral("beta"))}));
        set.addRule(makeRule(QStringLiteral("alpha"), 100, MatchExpression{}, {engineMode(QStringLiteral("alpha"))}));
        RuleEvaluator eval(set);
        const auto action = eval.resolve(konsoleQuery()).slot(QString(ActionSlot::EngineMode));
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QStringLiteral("mode")).toString(), QStringLiteral("gamma"));
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
        // Both event-scoped slots are filled — and each carries its own
        // effectId AND its own event param, proving the two events resolve
        // into independent slots with the correct action in each (rather than
        // one clobbering the other or an action landing in the wrong slot).
        const auto open = result.slot(QStringLiteral("anim-shader:window.open"));
        const auto close = result.slot(QStringLiteral("anim-shader:window.close"));
        QVERIFY(open.has_value());
        QVERIFY(close.has_value());
        QCOMPARE(open->params.value(QStringLiteral("event")).toString(), QStringLiteral("window.open"));
        QCOMPARE(open->params.value(QStringLiteral("effectId")).toString(), QStringLiteral("dissolve"));
        QCOMPARE(close->params.value(QStringLiteral("event")).toString(), QStringLiteral("window.close"));
        QCOMPARE(close->params.value(QStringLiteral("effectId")).toString(), QStringLiteral("popout"));
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

    // ── per-property appearance slots cascade independently ──

    void testBorderAppearance_perSlotCascade()
    {
        // Per-property correctness: a high-priority width rule and a separate
        // lower-priority colour rule occupy DIFFERENT slots, so BOTH apply;
        // a second width rule (lowest priority) is shadowed by the higher one
        // on the SAME slot. This is the behaviour that makes per-property
        // actions strictly better than a single grouped appearance blob.
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("width-high"), 500, MatchExpression{}, {borderWidth(4)}));
        set.addRule(
            makeRule(QStringLiteral("color-low"), 100, MatchExpression{}, {borderColor(QStringLiteral("#ff0000"))}));
        set.addRule(makeRule(QStringLiteral("width-lowest"), 50, MatchExpression{}, {borderWidth(9)}));
        RuleEvaluator eval(set);
        const ResolvedActions resolved = eval.resolve(konsoleQuery());

        const auto width = resolved.slot(QString(ActionSlot::BorderWidth));
        QVERIFY(width.has_value());
        QCOMPARE(width->params.value(QString(ActionParam::Value)).toInt(), 4); // higher-priority width wins its slot

        const auto color = resolved.slot(QString(ActionSlot::BorderColor));
        QVERIFY(color.has_value()); // colour slot also filled — not shadowed by the width rules
        QCOMPARE(color->params.value(QString(ActionParam::Value)).toString(), QStringLiteral("#ff0000"));
    }
};

QTEST_GUILESS_MAIN(TestRuleEvaluator)
#include "test_ruleevaluator.moc"

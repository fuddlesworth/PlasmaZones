// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QSet>
#include <QTest>

using namespace PhosphorWindowRules;
using namespace PhosphorWindowRules::TestHelpers;

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

    // ── hasMatchTargetingFields ──

    void testHasMatchTargetingFields()
    {
        const QSet<Field> transientFields = {Field::IsTransient, Field::WindowType, Field::IsModal};

        // The real-world scenario: a class-only "firefox → dissolve open" rule.
        // A Firefox tooltip shares the class, so it MATCHES the rule — but the
        // rule references no type field, so it must NOT count as deliberately
        // targeting the transient type.
        WindowRuleSet classOnly;
        classOnly.addRule(
            makeRule(QStringLiteral("firefox dissolve"), 100,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {overrideShader(QStringLiteral("window.open"), QStringLiteral("dissolve"))}));
        RuleEvaluator classEval(classOnly);

        WindowQuery firefoxTooltip;
        firefoxTooltip.windowClass = QStringLiteral("firefox");
        firefoxTooltip.isTransient = true;
        // Matches by class, but no type-field targeting → false.
        QVERIFY(classEval.hasAnyMatch(firefoxTooltip));
        QVERIFY(!classEval.hasMatchTargetingFields(firefoxTooltip, transientFields));

        // A rule that explicitly references a type field (here windowType,
        // mirroring the user's PiP rule) DOES count — and only when it also
        // matches the window. WindowType::Dialog == 2 on the wire.
        WindowRuleSet typed;
        typed.addRule(makeRule(
            QStringLiteral("firefox PiP"), 100,
            MatchExpression::makeAll({
                MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                MatchExpression::makeNone({MatchExpression::makeLeaf(
                    Field::WindowType, Operator::Equals, static_cast<int>(PhosphorProtocol::WindowType::Dialog))}),
            }),
            {overrideShader(QStringLiteral("window.open"), QStringLiteral("fade"))}));
        RuleEvaluator typedEval(typed);

        WindowQuery firefoxWin;
        firefoxWin.windowClass = QStringLiteral("firefox");
        firefoxWin.windowType = PhosphorProtocol::WindowType::Normal; // != Dialog(2) → passes the none{} clause
        firefoxWin.isTransient = true;
        QVERIFY(typedEval.hasMatchTargetingFields(firefoxWin, transientFields));

        // References the field but does NOT match the window (Dialog==2 fails
        // the none{} clause) → false: targeting requires an actual match.
        WindowQuery dialogWin;
        dialogWin.windowClass = QStringLiteral("firefox");
        dialogWin.windowType = PhosphorProtocol::WindowType::Dialog;
        dialogWin.isTransient = true;
        QVERIFY(!typedEval.hasMatchTargetingFields(dialogWin, transientFields));

        // A disabled type-targeting rule does not count.
        WindowRuleSet disabled;
        WindowRule r = makeRule(QStringLiteral("transient rule"), 100,
                                MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true),
                                {overrideShader(QStringLiteral("window.open"), QStringLiteral("fade"))});
        r.enabled = false;
        disabled.addRule(r);
        RuleEvaluator disabledEval(disabled);
        QVERIFY(!disabledEval.hasMatchTargetingFields(firefoxTooltip, transientFields));

        // Multi-rule set (the production shape): a non-targeting class-only
        // rule listed first plus a later type-targeting rule. The existence
        // check must find the targeting rule regardless of list position.
        WindowRuleSet mixed;
        mixed.addRule(
            makeRule(QStringLiteral("class only"), 100,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {overrideShader(QStringLiteral("window.open"), QStringLiteral("dissolve"))}));
        mixed.addRule(makeRule(QStringLiteral("targets transient"), 90,
                               MatchExpression::makeLeaf(Field::IsTransient, Operator::Equals, true),
                               {overrideShader(QStringLiteral("window.open"), QStringLiteral("fade"))}));
        RuleEvaluator mixedEval(mixed);
        QVERIFY(mixedEval.hasMatchTargetingFields(firefoxTooltip, transientFields));

        // The OSD/notification escape set {IsNotification, WindowType} is the
        // other production field set. A rule targeting IsNotification re-enables
        // an OSD/notification window the same way.
        const QSet<Field> osdFields = {Field::IsNotification, Field::WindowType};
        WindowRuleSet osd;
        osd.addRule(makeRule(QStringLiteral("targets notification"), 100,
                             MatchExpression::makeLeaf(Field::IsNotification, Operator::Equals, true),
                             {overrideShader(QStringLiteral("window.open"), QStringLiteral("fade"))}));
        RuleEvaluator osdEval(osd);
        WindowQuery notif;
        notif.windowClass = QStringLiteral("firefox");
        notif.isNotification = true;
        QVERIFY(osdEval.hasMatchTargetingFields(notif, osdFields));
        // The class-only firefox rule MATCHES this notification by class but
        // references no type field → does not re-enable it through the OSD set.
        QVERIFY(classEval.hasAnyMatch(notif));
        QVERIFY(!classEval.hasMatchTargetingFields(notif, osdFields));
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

    // The read-only peek used by the effect's per-frame paint path to skip the
    // (expensive) WindowQuery build on a cache hit. It must be revision-gated and
    // must not mutate the cache.
    void testResolveCachedIfPresent_peeksWithoutResolving()
    {
        WindowRuleSet set;
        set.addRule(makeRule(QStringLiteral("a"), 100, MatchExpression{}, {floatAction()}));
        RuleEvaluator eval(set);
        const QString winId = QStringLiteral("org.kde.konsole|abc");

        // (a) Unseeded cache → miss (nullopt), no entry created by the peek.
        QVERIFY(!eval.resolveCachedIfPresent(winId).has_value());
        QCOMPARE(eval.cacheSize(), 0);

        // (b) After resolveCached seeds the entry, the peek returns the SAME verdict
        //     and does NOT grow the cache.
        const ResolvedActions resolved = eval.resolveCached(winId, konsoleQuery());
        const std::optional<ResolvedActions> peeked = eval.resolveCachedIfPresent(winId);
        QVERIFY(peeked.has_value());
        QVERIFY(*peeked == resolved);
        QCOMPARE(eval.cacheSize(), 1);

        // (c) A rule-set mutation bumps the revision, so the now-stale entry reads as
        //     a miss — the peek is revision-gated exactly like resolveCached.
        set.addRule(makeRule(QStringLiteral("b"), 50, MatchExpression{}, {floatAction()}));
        QVERIFY(!eval.resolveCachedIfPresent(winId).has_value());
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
        set.addRule(makeRule(QStringLiteral("color-inactive"), 90, MatchExpression{},
                             {borderColorInactive(QStringLiteral("#00ff00"))}));
        set.addRule(makeRule(QStringLiteral("width-lowest"), 50, MatchExpression{}, {borderWidth(9)}));
        RuleEvaluator eval(set);
        const ResolvedActions resolved = eval.resolve(konsoleQuery());

        const auto width = resolved.slot(QString(ActionSlot::BorderWidth));
        QVERIFY(width.has_value());
        QCOMPARE(width->params.value(QString(ActionParam::Value)).toInt(), 4); // higher-priority width wins its slot

        // The focused and unfocused colours land on independent slots, each
        // carrying its colour in the single `value` param.
        const auto activeColor = resolved.slot(QString(ActionSlot::BorderColorActive));
        QVERIFY(activeColor.has_value());
        QCOMPARE(activeColor->params.value(QString(ActionParam::Value)).toString(), QStringLiteral("#ff0000"));
        const auto inactiveColor = resolved.slot(QString(ActionSlot::BorderColorInactive));
        QVERIFY(inactiveColor.has_value());
        QCOMPARE(inactiveColor->params.value(QString(ActionParam::Value)).toString(), QStringLiteral("#00ff00"));
    }
};

QTEST_GUILESS_MAIN(TestRuleEvaluator)
#include "test_ruleevaluator.moc"

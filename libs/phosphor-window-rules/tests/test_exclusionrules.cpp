// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_exclusionrules.cpp
 * @brief Unit tests for the public `ExclusionRules` slicers.
 *
 * Covers every public function in `PhosphorWindowRules::ExclusionRules`:
 *   - `excludeRulesFrom` (action-type filter; preserves ids / priority /
 *      match; drops disabled rules; excludes `ExcludeAnimations`)
 *   - `excludeAnimationsRulesFrom` (filters `ExcludeAnimations` rules;
 *      excludes generic `Exclude`; disjoint from `excludeRulesFrom`)
 *   - `applicationExcludePatternsFrom` (harvests AppId AppIdMatches
 *      leaves; drops empty / whitespace / non-leaf / non-AppId /
 *      non-AppIdMatches / disabled / non-Exclude rules; trims
 *      surviving patterns)
 *
 * The internal `ruleHasAction` / `rulesWithAction` predicates are
 * file-local in `src/exclusionrules.cpp` — they're exercised
 * transitively through the three public slicers above.
 *
 * The v3→v4 migration tests (`test_migration_v3_to_v4.cpp`) exercise
 * the end-to-end id derivation; these tests cover the slicers in
 * isolation so a regression in slicing semantics fails closer to the
 * source.
 */

#include "RuleTestHelpers.h"

#include <PhosphorWindowRules/ExclusionRules.h>

#include <QTest>

using namespace PhosphorWindowRules;
using namespace PhosphorWindowRules::TestHelpers;
namespace ER = PhosphorWindowRules::ExclusionRules;

class TestExclusionRules : public QObject
{
    Q_OBJECT

private:
    static RuleAction excludeAnimationsActionInstance()
    {
        RuleAction a;
        a.type = QString(ActionType::ExcludeAnimations);
        return a;
    }

    static WindowRule appIdExcludeRule(const QString& pattern, int priority = 0, bool enabled = true)
    {
        WindowRule r =
            makeRule(QStringLiteral("exclude-") + pattern, priority,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern), {excludeAction()});
        r.enabled = enabled;
        return r;
    }

    static WindowRule windowClassContainsAnimExcludeRule(const QString& pattern)
    {
        return makeRule(QStringLiteral("anim-") + pattern, 0,
                        MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, pattern),
                        {excludeAnimationsActionInstance()});
    }

    static WindowRule floatRule()
    {
        return makeRule(QStringLiteral("float"), 0, MatchExpression{}, {floatAction()});
    }

    static bool containsRuleId(const WindowRuleSet& set, const QUuid& id)
    {
        for (const WindowRule& r : set.rules()) {
            if (r.id == id) {
                return true;
            }
        }
        return false;
    }

private Q_SLOTS:

    // ── excludeRulesFrom ──────────────────────────────────────────────────

    void testExcludeRulesFrom_emptySource()
    {
        const WindowRuleSet source;
        const WindowRuleSet sliced = ER::excludeRulesFrom(source);
        QVERIFY(sliced.isEmpty());
    }

    void testExcludeRulesFrom_keepsExcludeDropsOthers()
    {
        WindowRuleSet source;
        const WindowRule e1 = appIdExcludeRule(QStringLiteral("firefox"));
        const WindowRule e2 = appIdExcludeRule(QStringLiteral("konsole"));
        const WindowRule a = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        const WindowRule f = floatRule();
        QVERIFY(source.addRule(e1));
        QVERIFY(source.addRule(f));
        QVERIFY(source.addRule(e2));
        QVERIFY(source.addRule(a));

        const WindowRuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 2);
        QVERIFY(containsRuleId(sliced, e1.id));
        QVERIFY(containsRuleId(sliced, e2.id));
        // ExcludeAnimations and Float rules must NOT leak into the
        // Exclude slice (they live on different cascade slots).
        QVERIFY(!containsRuleId(sliced, a.id));
        QVERIFY(!containsRuleId(sliced, f.id));
    }

    void testExcludeRulesFrom_skipsDisabledRules()
    {
        // The contract: disabled rules are skipped at slicing time so the
        // derived set is the MINIMUM admitted by the user. The downstream
        // RuleEvaluator does gate on `enabled` at resolve time too, but
        // carrying disabled rules through the slice would inflate the
        // priority-order index and lie to `!isEmpty()` fast-path callers.
        WindowRuleSet source;
        WindowRule disabled = appIdExcludeRule(QStringLiteral("firefox"));
        disabled.enabled = false;
        const WindowRule enabled = appIdExcludeRule(QStringLiteral("konsole"));
        QVERIFY(source.addRule(disabled));
        QVERIFY(source.addRule(enabled));

        const WindowRuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, enabled.id));
        QVERIFY(!containsRuleId(sliced, disabled.id));
    }

    void testExcludeRulesFrom_preservesIdsPriorityAndMatch()
    {
        // The slice is fed to a downstream RuleEvaluator that resolves by
        // priority and matches by id; a regression that re-derives priority
        // (e.g. renormalisation) or re-derives the match (e.g. forces
        // catch-all) would silently break the daemon's exclusion cascade.
        WindowRuleSet source;
        const WindowRule e = appIdExcludeRule(QStringLiteral("firefox"), /*priority=*/250);
        QVERIFY(source.addRule(e));

        const WindowRuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        const WindowRule out = sliced.rules().first();
        QCOMPARE(out.id, e.id);
        QCOMPARE(out.priority, 250);
        QCOMPARE(out.match.kind(), MatchExpression::Kind::Leaf);
        QCOMPARE(out.match.predicate().field, Field::AppId);
        QCOMPARE(out.match.predicate().op, Operator::AppIdMatches);
        QCOMPARE(out.match.predicate().value.toString(), QStringLiteral("firefox"));
    }

    void testExcludeRulesFrom_keepsMultiActionRuleWhenAnyMatches()
    {
        // A rule carrying both Float AND Exclude is still an Exclude
        // rule for the snap-gate's purposes — it satisfies the action-
        // type filter on the Exclude side.
        WindowRuleSet source;
        WindowRule multi =
            makeRule(QStringLiteral("multi"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {floatAction(), excludeAction()});
        QVERIFY(source.addRule(multi));

        const WindowRuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, multi.id));
    }

    // ── excludeAnimationsRulesFrom ────────────────────────────────────────

    void testExcludeAnimationsRulesFrom_filtersByActionType()
    {
        WindowRuleSet source;
        const WindowRule e = appIdExcludeRule(QStringLiteral("firefox"));
        const WindowRule a = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        const WindowRule f = floatRule();
        QVERIFY(source.addRule(e));
        QVERIFY(source.addRule(a));
        QVERIFY(source.addRule(f));

        const WindowRuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, a.id));
        QVERIFY(!containsRuleId(sliced, e.id));
        QVERIFY(!containsRuleId(sliced, f.id));
    }

    void testExcludeAnimationsRulesFrom_disjointFromExcludeRulesFrom()
    {
        // A rule carries either Exclude or ExcludeAnimations (or neither)
        // for the migration-produced shape. The two slicers must produce
        // disjoint result sets so the snap-gate and the animation-gate
        // never see the same rule.
        WindowRuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        QVERIFY(source.addRule(windowClassContainsAnimExcludeRule(QStringLiteral("steam"))));

        const WindowRuleSet snapSlice = ER::excludeRulesFrom(source);
        const WindowRuleSet animSlice = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(snapSlice.count(), 1);
        QCOMPARE(animSlice.count(), 1);
        for (const WindowRule& s : snapSlice.rules()) {
            QVERIFY(!containsRuleId(animSlice, s.id));
        }
    }

    void testExcludeAnimationsRulesFrom_emptySource()
    {
        const WindowRuleSet source;
        const WindowRuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QVERIFY(sliced.isEmpty());
    }

    void testExcludeAnimationsRulesFrom_preservesIdsPriorityAndMatch()
    {
        // Parity with testExcludeRulesFrom_preservesIdsPriorityAndMatch —
        // both slicers dispatch through the same file-local rulesWithAction
        // helper, so a regression in priority/id/match preservation could
        // only affect one of them through a future divergence. Pin
        // independently so the contract risk stays symmetric.
        WindowRuleSet source;
        WindowRule a =
            makeRule(QStringLiteral("anim-pri"), /*priority=*/175,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("steam")),
                     {excludeAnimationsActionInstance()});
        QVERIFY(source.addRule(a));

        const WindowRuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        const WindowRule out = sliced.rules().first();
        QCOMPARE(out.id, a.id);
        QCOMPARE(out.priority, 175);
        QCOMPARE(out.match.kind(), MatchExpression::Kind::Leaf);
        QCOMPARE(out.match.predicate().field, Field::WindowClass);
        QCOMPARE(out.match.predicate().op, Operator::Contains);
        QCOMPARE(out.match.predicate().value.toString(), QStringLiteral("steam"));
    }

    void testExcludeAnimationsRulesFrom_keepsMultiActionRuleWhenAnyMatches()
    {
        // Parity with the snap-side multi-action test — a rule carrying
        // Float + ExcludeAnimations still appears in the animations slice
        // because the action-type filter matches on ANY action's type.
        WindowRuleSet source;
        WindowRule multi =
            makeRule(QStringLiteral("multi-anim"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {floatAction(), excludeAnimationsActionInstance()});
        QVERIFY(source.addRule(multi));

        const WindowRuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, multi.id));
    }

    void testSlicers_ruleWithBothExcludeAndExcludeAnimationsAppearsInBothSlices()
    {
        // The migration produces rules with exactly one of Exclude /
        // ExcludeAnimations, but the WindowRule schema doesn't forbid a
        // hand-authored rule carrying BOTH on the same `actions` list.
        // The current contract: a multi-action rule with both action
        // types appears in BOTH slices. Pin this so a future "disjoint
        // slices" optimisation can't silently change the resolution
        // shape — that change would have to update this assertion AND
        // gate one of the slicers / action validator to refuse the
        // combination on input.
        WindowRuleSet source;
        WindowRule both =
            makeRule(QStringLiteral("both"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {excludeAction(), excludeAnimationsActionInstance()});
        QVERIFY(source.addRule(both));

        const WindowRuleSet snapSlice = ER::excludeRulesFrom(source);
        const WindowRuleSet animSlice = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(snapSlice.count(), 1);
        QCOMPARE(animSlice.count(), 1);
        QVERIFY(containsRuleId(snapSlice, both.id));
        QVERIFY(containsRuleId(animSlice, both.id));
    }

    void testExcludeAnimationsRulesFrom_skipsDisabledRules()
    {
        // Same disabled-skip contract as the snap slicer above.
        WindowRuleSet source;
        WindowRule disabled = windowClassContainsAnimExcludeRule(QStringLiteral("firefox"));
        disabled.enabled = false;
        const WindowRule enabled = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        QVERIFY(source.addRule(disabled));
        QVERIFY(source.addRule(enabled));

        const WindowRuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, enabled.id));
    }

    // ── applicationExcludePatternsFrom ────────────────────────────────────

    void testApplicationExcludePatternsFrom_harvestsAppIdLeaves()
    {
        WindowRuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("konsole"))));

        QStringList patterns = ER::applicationExcludePatternsFrom(source);
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox") << QStringLiteral("konsole"));
    }

    void testApplicationExcludePatternsFrom_dropsNonExcludeRules()
    {
        WindowRuleSet source;
        // An AppId AppIdMatches leaf with a Float action — not an Exclude.
        // The pending-restore prune callers ONLY care about Exclude
        // patterns; a Float-action rule must not surface here.
        QVERIFY(source.addRule(
            makeRule(QStringLiteral("not-exclude"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {floatAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }

    void testApplicationExcludePatternsFrom_dropsDisabledRules()
    {
        // Symmetric with the slice-side disabled-skip — the daemon's
        // pending-restore prune consumes the returned patterns, and
        // pruning restores for a DISABLED rule's app would silently kill
        // state the user explicitly opted to keep.
        WindowRuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"), 0, /*enabled=*/false)));
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("konsole"), 0, /*enabled=*/true)));

        const QStringList patterns = ER::applicationExcludePatternsFrom(source);
        QCOMPARE(patterns, QStringList{} << QStringLiteral("konsole"));
    }

    void testApplicationExcludePatternsFrom_dropsNonAppIdField()
    {
        // A WindowClass-leaf Exclude rule (user-authored via the Rule
        // editor's broader UI) is a valid Exclude — it fires through the
        // snap engine — but it doesn't have a single canonical AppId
        // pattern to harvest for the pending-restore prune. The helper's
        // documented contract is that composite / non-AppId rules are
        // SILENTLY skipped; pinning that here means a future widening
        // would have to update this test deliberately.
        WindowRuleSet source;
        QVERIFY(source.addRule(
            makeRule(QStringLiteral("wc-exclude"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("steam")),
                     {excludeAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }

    void testApplicationExcludePatternsFrom_dropsNonAppIdMatchesOperator()
    {
        // An AppId-leaf Exclude rule whose operator is Equals (not
        // AppIdMatches) is also a valid Exclude but doesn't carry the
        // segment-aware reverse-DNS match semantics the prune walker
        // relies on. Drop it — same contract as the field check above.
        WindowRuleSet source;
        QVERIFY(source.addRule(makeRule(
            QStringLiteral("appId-equals"), 0,
            MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox")), {excludeAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }

    void testApplicationExcludePatternsFrom_dropsNonLeafMatch()
    {
        // A composite (All / Any / None) match is structurally not a
        // single-pattern rule — even if every leaf is AppId AppIdMatches,
        // there's no canonical pattern to harvest. Drop it.
        WindowRuleSet source;
        const MatchExpression composite = MatchExpression::makeAll(
            {MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
             MatchExpression::makeLeaf(Field::Title, Operator::Contains, QStringLiteral("private"))});
        QVERIFY(source.addRule(makeRule(QStringLiteral("composite"), 0, composite, {excludeAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }

    void testApplicationExcludePatternsFrom_dropsWhitespacePatterns()
    {
        // The leaf value goes through `.trimmed()` then `.isEmpty()` so
        // an all-whitespace pattern is dropped at harvest time. (The
        // WindowRule loader accepts whitespace-only values because the
        // structural validator only checks op/field compatibility; this
        // is the canonical post-load sweep.)
        WindowRuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("   "))));
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        const QStringList patterns = ER::applicationExcludePatternsFrom(source);
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox"));
    }

    void testApplicationExcludePatternsFrom_trimsSurvivingPatterns()
    {
        // Padding whitespace on a non-empty pattern is stripped — same
        // semantics as the migration's `pattern.trimmed()` skip so a
        // padded user-authored rule and a migrated rule produce
        // byte-identical harvest output.
        WindowRuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("  firefox  "))));
        const QStringList patterns = ER::applicationExcludePatternsFrom(source);
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox"));
    }

    void testApplicationExcludePatternsFrom_emptySource()
    {
        const WindowRuleSet source;
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }
};

QTEST_GUILESS_MAIN(TestExclusionRules)
#include "test_exclusionrules.moc"

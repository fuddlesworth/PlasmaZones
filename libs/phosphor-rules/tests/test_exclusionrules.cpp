// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_exclusionrules.cpp
 * @brief Unit tests for the public `ExclusionRules` slicers.
 *
 * Covers every public function in `PhosphorRules::ExclusionRules`:
 *   - `excludeRulesFrom` (action-type filter; preserves ids / priority /
 *      match; drops disabled rules; excludes `ExcludeAnimations` and the
 *      scoped `ExcludePlacement` / `ExcludeDecorations` siblings)
 *   - `excludePlacementRulesFrom` (union slice: Exclude ∪
 *      ExcludePlacement; excludes the decoration/animation-scoped types)
 *   - `excludeDecorationsRulesFrom` (union slice: Exclude ∪
 *      ExcludeDecorations; excludes the placement/animation-scoped types)
 *   - `excludeAnimationsRulesFrom` (filters `ExcludeAnimations` rules;
 *      excludes generic `Exclude`; disjoint from `excludeRulesFrom`)
 *   - `applicationExcludePatternsFrom` (harvests AppId AppIdMatches
 *      leaves from Exclude AND ExcludePlacement rules; drops empty /
 *      whitespace / non-leaf / non-AppId / non-AppIdMatches / disabled /
 *      other-action rules; trims surviving patterns)
 *
 * The internal predicates (`ruleHasAction`, `rulesWithAction`,
 * `rulesWithEitherAction`, `isPlacementExclusion`) are file-local in
 * `src/exclusionrules.cpp` — they're exercised transitively through the
 * four public slicers plus the pattern harvester above.
 *
 * The v3→v4 migration tests (`test_migration_v3_to_v4.cpp`) exercise
 * the end-to-end id derivation; these tests cover the slicers in
 * isolation so a regression in slicing semantics fails closer to the
 * source.
 */

#include "RuleTestHelpers.h"

#include <PhosphorRules/ExclusionRules.h>

#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;
namespace ER = PhosphorRules::ExclusionRules;

class TestExclusionRules : public QObject
{
    Q_OBJECT

private:
    // Action constructors live in RuleTestHelpers.h with the rest of the
    // suite's shared shapes (excludeAction / floatAction /
    // excludePlacementAction / excludeDecorationsAction /
    // excludeAnimationsAction).

    static Rule appIdExcludeRule(const QString& pattern, int priority = 0, bool enabled = true)
    {
        Rule r = makeRule(QStringLiteral("exclude-") + pattern, priority,
                          MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, pattern), {excludeAction()});
        r.enabled = enabled;
        return r;
    }

    static Rule windowClassContainsAnimExcludeRule(const QString& pattern)
    {
        return makeRule(QStringLiteral("anim-") + pattern, 0,
                        MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, pattern),
                        {excludeAnimationsAction()});
    }

    static Rule floatRule()
    {
        return makeRule(QStringLiteral("float"), 0, MatchExpression{}, {floatAction()});
    }

    static bool containsRuleId(const RuleSet& set, const QUuid& id)
    {
        for (const Rule& r : set.rules()) {
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
        const RuleSet source;
        const RuleSet sliced = ER::excludeRulesFrom(source);
        QVERIFY(sliced.isEmpty());
    }

    void testExcludeRulesFrom_keepsExcludeDropsOthers()
    {
        RuleSet source;
        const Rule e1 = appIdExcludeRule(QStringLiteral("firefox"));
        const Rule e2 = appIdExcludeRule(QStringLiteral("konsole"));
        const Rule a = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        const Rule f = floatRule();
        QVERIFY(source.addRule(e1));
        QVERIFY(source.addRule(f));
        QVERIFY(source.addRule(e2));
        QVERIFY(source.addRule(a));

        const RuleSet sliced = ER::excludeRulesFrom(source);
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
        RuleSet source;
        Rule disabled = appIdExcludeRule(QStringLiteral("firefox"));
        disabled.enabled = false;
        const Rule enabled = appIdExcludeRule(QStringLiteral("konsole"));
        QVERIFY(source.addRule(disabled));
        QVERIFY(source.addRule(enabled));

        const RuleSet sliced = ER::excludeRulesFrom(source);
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
        RuleSet source;
        const Rule e = appIdExcludeRule(QStringLiteral("firefox"), /*priority=*/250);
        QVERIFY(source.addRule(e));

        const RuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        const Rule out = sliced.rules().first();
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
        RuleSet source;
        Rule multi =
            makeRule(QStringLiteral("multi"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {floatAction(), excludeAction()});
        QVERIFY(source.addRule(multi));

        const RuleSet sliced = ER::excludeRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, multi.id));
    }

    void testExcludeRulesFrom_dropsScopedSiblings()
    {
        // The blanket-only slice must NOT pick up the scoped siblings —
        // excludeRulesFrom stays the Exclude-shape-alone slicer for any
        // consumer that needs exactly the blanket rules.
        RuleSet source;
        const Rule p = makeRule(
            QStringLiteral("placement"), 0,
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("org.deskflow.deskflow")),
            {excludePlacementAction()});
        const Rule d =
            makeRule(QStringLiteral("deco"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("mpv")),
                     {excludeDecorationsAction()});
        QVERIFY(source.addRule(p));
        QVERIFY(source.addRule(d));
        QVERIFY(ER::excludeRulesFrom(source).isEmpty());
    }

    // ── excludePlacementRulesFrom / excludeDecorationsRulesFrom ──────────

    void testExcludePlacementRulesFrom_unionOfBlanketAndScoped()
    {
        // The placement slice the daemon and the effect's drag gate bind:
        // blanket Exclude keeps its historical placement effect and the
        // scoped ExcludePlacement joins it; decoration- and animation-
        // scoped rules stay out.
        RuleSet source;
        const Rule blanket = appIdExcludeRule(QStringLiteral("firefox"));
        const Rule scoped = makeRule(
            QStringLiteral("placement"), 0,
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("org.deskflow.deskflow")),
            {excludePlacementAction()});
        const Rule deco =
            makeRule(QStringLiteral("deco"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("mpv")),
                     {excludeDecorationsAction()});
        const Rule anim = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        const Rule f = floatRule();
        QVERIFY(source.addRule(blanket));
        QVERIFY(source.addRule(scoped));
        QVERIFY(source.addRule(deco));
        QVERIFY(source.addRule(anim));
        QVERIFY(source.addRule(f));

        const RuleSet sliced = ER::excludePlacementRulesFrom(source);
        QCOMPARE(sliced.count(), 2);
        QVERIFY(containsRuleId(sliced, blanket.id));
        QVERIFY(containsRuleId(sliced, scoped.id));
        QVERIFY(!containsRuleId(sliced, deco.id));
        QVERIFY(!containsRuleId(sliced, anim.id));
        QVERIFY(!containsRuleId(sliced, f.id));
    }

    void testExcludeDecorationsRulesFrom_unionOfBlanketAndScoped()
    {
        // The decoration slice shouldDecorateWindow binds: blanket Exclude
        // keeps stripping decorations, the scoped ExcludeDecorations joins
        // it, and the placement-scoped sibling stays out (a window excluded
        // from tiling keeps its decorations).
        RuleSet source;
        const Rule blanket = appIdExcludeRule(QStringLiteral("firefox"));
        const Rule scoped =
            makeRule(QStringLiteral("deco"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("mpv")),
                     {excludeDecorationsAction()});
        const Rule placement = makeRule(
            QStringLiteral("placement"), 0,
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("org.deskflow.deskflow")),
            {excludePlacementAction()});
        const Rule anim = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        QVERIFY(source.addRule(blanket));
        QVERIFY(source.addRule(scoped));
        QVERIFY(source.addRule(placement));
        QVERIFY(source.addRule(anim));

        const RuleSet sliced = ER::excludeDecorationsRulesFrom(source);
        QCOMPARE(sliced.count(), 2);
        QVERIFY(containsRuleId(sliced, blanket.id));
        QVERIFY(containsRuleId(sliced, scoped.id));
        QVERIFY(!containsRuleId(sliced, placement.id));
        QVERIFY(!containsRuleId(sliced, anim.id));
    }

    void testUnionSlicers_skipDisabledRules()
    {
        // Same disabled-skip contract as the single-action slicers — the
        // union helper walks its own loop, so pin it independently.
        RuleSet source;
        Rule disabledScoped =
            makeRule(QStringLiteral("placement-off"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {excludePlacementAction()});
        disabledScoped.enabled = false;
        Rule disabledDeco =
            makeRule(QStringLiteral("deco-off"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("mpv")),
                     {excludeDecorationsAction()});
        disabledDeco.enabled = false;
        QVERIFY(source.addRule(disabledScoped));
        QVERIFY(source.addRule(disabledDeco));

        QVERIFY(ER::excludePlacementRulesFrom(source).isEmpty());
        QVERIFY(ER::excludeDecorationsRulesFrom(source).isEmpty());
    }

    void testUnionSlicers_blanketExcludeRuleCountedOnceNotDuplicated()
    {
        // A rule carrying BOTH Exclude and ExcludePlacement satisfies the
        // union predicate twice; it must still appear exactly once.
        RuleSet source;
        Rule both = makeRule(QStringLiteral("both"), 0,
                             MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                             {excludeAction(), excludePlacementAction()});
        QVERIFY(source.addRule(both));

        const RuleSet sliced = ER::excludePlacementRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, both.id));
    }

    // ── excludeAnimationsRulesFrom ────────────────────────────────────────

    void testExcludeAnimationsRulesFrom_filtersByActionType()
    {
        RuleSet source;
        const Rule e = appIdExcludeRule(QStringLiteral("firefox"));
        const Rule a = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        const Rule f = floatRule();
        QVERIFY(source.addRule(e));
        QVERIFY(source.addRule(a));
        QVERIFY(source.addRule(f));

        const RuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, a.id));
        QVERIFY(!containsRuleId(sliced, e.id));
        QVERIFY(!containsRuleId(sliced, f.id));
    }

    void testExcludePlacementRulesFrom_preservesIdsPriorityAndMatch()
    {
        // Preservation parity for the union path: the placement slicer walks
        // its own loop (shared membership predicate with the harvester), so
        // pin id/priority/match preservation independently of the
        // rulesWithAction tests — a regression that reconstructed rules
        // instead of copying them would otherwise pass the whole suite.
        RuleSet source;
        Rule scoped = makeRule(
            QStringLiteral("placement-pri"), /*priority=*/220,
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("org.deskflow.deskflow")),
            {excludePlacementAction()});
        QVERIFY(source.addRule(scoped));

        const RuleSet sliced = ER::excludePlacementRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        const Rule out = sliced.rules().first();
        QCOMPARE(out.id, scoped.id);
        QCOMPARE(out.priority, 220);
        QCOMPARE(out.match.kind(), MatchExpression::Kind::Leaf);
        QCOMPARE(out.match.predicate().field, Field::AppId);
        QCOMPARE(out.match.predicate().op, Operator::AppIdMatches);
        QCOMPARE(out.match.predicate().value.toString(), QStringLiteral("org.deskflow.deskflow"));
    }

    void testExcludeAnimationsRulesFrom_disjointFromExcludeRulesFrom()
    {
        // For the migration-produced ONE-ACTION-PER-RULE shape, a rule
        // carries either Exclude or ExcludeAnimations, so the two slicers
        // produce disjoint result sets. This is a property of that shape,
        // not a universal — a hand-authored rule carrying BOTH actions
        // appears in both slices, pinned by
        // testSlicers_ruleWithBothExcludeAndExcludeAnimationsAppearsInBothSlices.
        RuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        QVERIFY(source.addRule(windowClassContainsAnimExcludeRule(QStringLiteral("steam"))));

        const RuleSet snapSlice = ER::excludeRulesFrom(source);
        const RuleSet animSlice = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(snapSlice.count(), 1);
        QCOMPARE(animSlice.count(), 1);
        for (const Rule& s : snapSlice.rules()) {
            QVERIFY(!containsRuleId(animSlice, s.id));
        }
    }

    void testExcludeAnimationsRulesFrom_emptySource()
    {
        const RuleSet source;
        const RuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QVERIFY(sliced.isEmpty());
    }

    void testExcludeAnimationsRulesFrom_preservesIdsPriorityAndMatch()
    {
        // Parity with testExcludeRulesFrom_preservesIdsPriorityAndMatch —
        // both slicers dispatch through the same file-local rulesWithAction
        // helper, so a regression in priority/id/match preservation could
        // only affect one of them through a future divergence. Pin
        // independently so the contract risk stays symmetric.
        RuleSet source;
        Rule a = makeRule(QStringLiteral("anim-pri"), /*priority=*/175,
                          MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("steam")),
                          {excludeAnimationsAction()});
        QVERIFY(source.addRule(a));

        const RuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        const Rule out = sliced.rules().first();
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
        RuleSet source;
        Rule multi =
            makeRule(QStringLiteral("multi-anim"), 0,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {floatAction(), excludeAnimationsAction()});
        QVERIFY(source.addRule(multi));

        const RuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, multi.id));
    }

    void testSlicers_ruleWithBothExcludeAndExcludeAnimationsAppearsInBothSlices()
    {
        // The migration produces rules with exactly one of Exclude /
        // ExcludeAnimations, but the Rule schema doesn't forbid a
        // hand-authored rule carrying BOTH on the same `actions` list.
        // The current contract: a multi-action rule with both action
        // types appears in BOTH slices. Pin this so a future "disjoint
        // slices" optimisation can't silently change the resolution
        // shape — that change would have to update this assertion AND
        // gate one of the slicers / action validator to refuse the
        // combination on input.
        RuleSet source;
        Rule both = makeRule(QStringLiteral("both"), 0,
                             MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                             {excludeAction(), excludeAnimationsAction()});
        QVERIFY(source.addRule(both));

        const RuleSet snapSlice = ER::excludeRulesFrom(source);
        const RuleSet animSlice = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(snapSlice.count(), 1);
        QCOMPARE(animSlice.count(), 1);
        QVERIFY(containsRuleId(snapSlice, both.id));
        QVERIFY(containsRuleId(animSlice, both.id));
    }

    void testExcludeAnimationsRulesFrom_skipsDisabledRules()
    {
        // Same disabled-skip contract as the snap slicer above.
        RuleSet source;
        Rule disabled = windowClassContainsAnimExcludeRule(QStringLiteral("firefox"));
        disabled.enabled = false;
        const Rule enabled = windowClassContainsAnimExcludeRule(QStringLiteral("steam"));
        QVERIFY(source.addRule(disabled));
        QVERIFY(source.addRule(enabled));

        const RuleSet sliced = ER::excludeAnimationsRulesFrom(source);
        QCOMPARE(sliced.count(), 1);
        QVERIFY(containsRuleId(sliced, enabled.id));
    }

    // ── applicationExcludePatternsFrom ────────────────────────────────────

    void testApplicationExcludePatternsFrom_harvestsAppIdLeaves()
    {
        RuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("konsole"))));

        QStringList patterns = ER::applicationExcludePatternsFrom(source);
        patterns.sort();
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox") << QStringLiteral("konsole"));
    }

    void testApplicationExcludePatternsFrom_deduplicatesAcrossRuleShapes()
    {
        // A blanket Exclude rule and a scoped ExcludePlacement rule for the
        // SAME app pattern (the natural pairing after the excludeApp template
        // retarget) harvest one entry, not two — the prune is idempotent, so
        // this pins the redundant-sweep removal, not a correctness property.
        RuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("firefox"))));
        QVERIFY(source.addRule(
            makeRule(QStringLiteral("placement-dup"), 10,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {excludePlacementAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{} << QStringLiteral("firefox"));
    }

    void testApplicationExcludePatternsFrom_harvestsExcludePlacementRules()
    {
        // ExcludePlacement makes a window unmanaged by placement exactly
        // like the blanket Exclude, and the pending-restore prune this
        // helper feeds is a placement concern — so its AppId patterns
        // harvest too.
        RuleSet source;
        QVERIFY(source.addRule(makeRule(
            QStringLiteral("placement"), 0,
            MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("org.deskflow.deskflow")),
            {excludePlacementAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{} << QStringLiteral("org.deskflow.deskflow"));
    }

    void testApplicationExcludePatternsFrom_dropsExcludeDecorationsRules()
    {
        // ExcludeDecorations is NOT a placement exclusion — pruning queued
        // restores for a decoration-only rule's app would kill placement
        // state the user never opted out of.
        RuleSet source;
        QVERIFY(source.addRule(
            makeRule(QStringLiteral("deco"), 0,
                     MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("firefox")),
                     {excludeDecorationsAction()})));
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }

    void testApplicationExcludePatternsFrom_dropsNonExcludeRules()
    {
        RuleSet source;
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
        RuleSet source;
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
        RuleSet source;
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
        RuleSet source;
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
        RuleSet source;
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
        // Rule loader accepts whitespace-only values because the
        // structural validator only checks op/field compatibility; this
        // is the canonical post-load sweep.)
        RuleSet source;
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
        RuleSet source;
        QVERIFY(source.addRule(appIdExcludeRule(QStringLiteral("  firefox  "))));
        const QStringList patterns = ER::applicationExcludePatternsFrom(source);
        QCOMPARE(patterns, QStringList{} << QStringLiteral("firefox"));
    }

    void testApplicationExcludePatternsFrom_emptySource()
    {
        const RuleSet source;
        QCOMPARE(ER::applicationExcludePatternsFrom(source), QStringList{});
    }
};

QTEST_GUILESS_MAIN(TestExclusionRules)
#include "test_exclusionrules.moc"

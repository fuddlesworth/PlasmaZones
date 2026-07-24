// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_cascade_fidelity.cpp
 * @brief Priority-wins proof for the window-rule context model.
 *
 * The RuleEvaluator resolves each action slot independently: the
 * highest-priority matching rule fills the slot, ties broken by stable list
 * order. There is no specificity formula, no synthesized provider-default
 * rule, and no catch-all exclusion. "Exact beats screen-only" or "activity
 * beats desktop" are not emergent from any formula — they hold here only
 * because each test assigns explicit priorities that reproduce that ordering
 * (screen-only 301, screen+desktop 303, screen+activity 304, exact 306). A
 * user-authored catch-all assignment rule (empty context dims) at a LOW
 * priority is the explicit floor every pinned rule outranks.
 *
 * This suite ports the behavioural scenarios from
 * tests/unit/core/test_layoutmanager_assignment.cpp — exact-match-wins,
 * activity-beats-desktop, screen-only display default, mode-only autotile
 * entries, catch-all fallback — and asserts a RuleEvaluator over the context
 * rules resolves each windowless context query to the engine-mode / layout
 * the assigned priorities select.
 *
 * SCOPE — this suite proves the rule MODEL is priority-correct: it exercises
 * the RuleEvaluator + ContextRuleBridge directly. LayoutRegistry's own
 * re-implementation on this model is verified separately by
 * tests/unit/core/test_layoutmanager_assignment.cpp (the assignment oracle
 * suite, which runs against the rule-backed registry). The connector-name /
 * virtual-screen fallback is a query-side retry loop in LayoutRegistry and is
 * out of this model-level suite's scope by design.
 *
 * The LayoutRegistry-boundary cases at the bottom of this file then close
 * the symmetric proof: they assert that the same predicate-evaluation guard
 * holds end-to-end through @ref PhosphorZones::LayoutRegistry, whose
 * @c resolveAssignmentEntry composes the evaluator with a structural filter
 * that admits mixed (context + window-property) rules. A mixed rule must
 * still be inert against the windowless queries the registry issues, and a
 * query matching no rule falls back to the registry's settings-gated default
 * resolver.
 *
 * The context-slot cascade proofs (gaps, orientation, active-layout, tiling
 * params, overlay, locks, per-mode gaps) live in the companion suite
 * tests/unit/core/test_rule_cascade_context.cpp, sharing the harness in
 * RuleCascadeFixture.h.
 */

#include <QString>
#include <QTest>
#include <QUuid>

#include "RuleCascadeFixture.h"

class TestRuleCascadeFidelity : public QObject, public RuleCascadeFixture
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Exact match wins ─────────────────────────────────────────────────
    // Ports testLayoutManager_layoutForScreen_fallbackCascade.

    void testExactMatchWins()
    {
        QList<PWR::Rule> rules;
        // Display default for DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{screen-layout}"), QString(),
                                             301));
        // Desktop 2 on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{desktop-layout}"), QString(),
                                             303));
        rules.append(CRB::makeAssignmentRule(QStringLiteral("Default"), QString(), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{global}"), QString(), 1));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // Desktop 2 → desktop-specific layout.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{desktop-layout}"));
        // Desktop 1 has no explicit entry → cascades to the DP-1 display default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{screen-layout}"));
        // A different screen → catch-all floor.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("HDMI-1"), 1, QString())),
                 QStringLiteral("{global}"));
    }

    // ─── Activity beats desktop ───────────────────────────────────────────
    // Ports testLayoutManager_layoutForScreen_activityWinsOverDesktop.

    void testActivityWinsOverDesktop()
    {
        QList<PWR::Rule> rules;
        // Desktop 2 on DP-1 (priority 303).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{desktop-two}"), QString(),
                                             303));
        // Activity "work" on DP-1, any desktop (priority 304).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("work-uuid"), QStringLiteral("snapping"),
                                             QStringLiteral("{activity-work}"), QString(), 304));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // In the work activity on desktop 2 → activity rule wins (304 > 303).
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QStringLiteral("work-uuid"))),
                 QStringLiteral("{activity-work}"));
        // No activity → desktop rule applies.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{desktop-two}"));
    }

    // ─── Per-activity cascade ─────────────────────────────────────────────
    // Ports testLayoutManager_layoutForScreen_perActivityCascade.

    void testPerActivityCascade()
    {
        QList<PWR::Rule> rules;
        // Monitor default for DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{monitor-default}"), QString(),
                                             301));
        // Work activity on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("activity-work"), QStringLiteral("snapping"),
                                             QStringLiteral("{work-activity}"), QString(), 304));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // In the work activity on any desktop → activity layout.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QStringLiteral("activity-work"))),
                 QStringLiteral("{work-activity}"));
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 5, QStringLiteral("activity-work"))),
                 QStringLiteral("{work-activity}"));
        // An activity with no per-activity entry → monitor default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QStringLiteral("activity-play"))),
                 QStringLiteral("{monitor-default}"));
        // Empty activity → monitor default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{monitor-default}"));
    }

    // ─── Autotile-mode guard ──────────────────────────────────────────────
    // The migrated autotile rule carries `setEngineMode = autotile` and a
    // `setTilingAlgorithm` action; the snapping layout is preserved as a
    // separate slot. Ports testAssignmentEntry_autotileAssignment_setsFields.

    void testAutotileModeRule()
    {
        QList<PWR::Rule> rules;
        // Autotile with both a snapping layout AND a tiling algorithm (the
        // mode-toggle-lossless shape).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("autotile"), QStringLiteral("{snap-preserved}"),
                                             QStringLiteral("dwindle"), 301));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        const PWR::WindowQuery q = contextQuery(QStringLiteral("DP-1"), 1, QString());
        // Engine-mode resolves to autotile.
        QCOMPARE(resolvedMode(evaluator, q), QStringLiteral("autotile"));
        // The layout slot is filled by the FIRST layout-slot action — both
        // SetSnappingLayout and SetTilingAlgorithm share the `layout` slot,
        // so the first one in action order wins (the snapping layout, by
        // ContextRuleBridge::makeAssignmentActions order).
        const PWR::ResolvedActions actions = evaluator.resolve(q);
        QVERIFY(actions.hasSlot(QString(PWR::ActionSlot::Layout)));
        // Crucially: the slot is won by the SetSnappingLayout action, NOT the
        // SetTilingAlgorithm. The resolved layout id must be the preserved
        // snapping layout — if SetTilingAlgorithm wrongly took the slot this
        // would resolve to "dwindle" instead.
        QCOMPARE(resolvedLayoutId(evaluator, q), QStringLiteral("{snap-preserved}"));
    }

    // ─── Mode-only autotile entry ─────────────────────────────────────────
    // Ports testAssignmentEntry_modeOnlyAutotile_cascadeAccepts: a KCM
    // "autotile, default algorithm" entry has both layout fields empty and
    // therefore migrates to a rule with ONLY a setEngineMode action.

    void testModeOnlyAutotileEntry()
    {
        QList<PWR::Rule> rules;
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("autotile"), QString(), QString(), 301));
        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        const PWR::WindowQuery q = contextQuery(QStringLiteral("DP-1"), 1, QString());
        // The mode is still resolved — the engine-mode slot is filled.
        QCOMPARE(resolvedMode(evaluator, q), QStringLiteral("autotile"));
        // No layout slot — the entry was mode-only.
        QVERIFY(!evaluator.resolve(q).hasSlot(QString(PWR::ActionSlot::Layout)));
    }

    // ─── Catch-all floor fallback ─────────────────────────────────────────
    // Ports testLevel1Default_* — a context with no pinned rule resolves to
    // the low-priority catch-all floor rule.

    void testCatchAllFloorFallback()
    {
        QList<PWR::Rule> rules;
        // One pinned rule for DP-1 only.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{dp1-layout}"), QString(),
                                             301));
        // Autotile catch-all floor.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("Default"), QString(), 0, QString(),
                                             QStringLiteral("autotile"), QString(), QStringLiteral("bsp"), 1));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // DP-1 hits the pinned rule.
        QCOMPARE(resolvedMode(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("snapping"));
        // HDMI-9 has no pinned rule → catch-all floor (autotile, bsp).
        const PWR::WindowQuery other = contextQuery(QStringLiteral("HDMI-9"), 3, QString());
        QCOMPARE(resolvedMode(evaluator, other), QStringLiteral("autotile"));
        QCOMPARE(resolvedLayoutId(evaluator, other), QStringLiteral("bsp"));
    }

    // ─── Catch-all floor never shadows a pinned rule ──────────────────────
    // The catch-all matches every query, but its low priority (1) means a
    // pinned rule (priority >= 301) always resolves first.

    void testCatchAllFloorNeverShadowsPinned()
    {
        QList<PWR::Rule> rules;
        rules.append(CRB::makeAssignmentRule(QStringLiteral("Default"), QString(), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{global}"), QString(), 1));
        // Add the pinned rule AFTER the floor so list order would favour the
        // floor if priority were ignored — priority must still win.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{specific}"), QString(), 303));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{specific}"));
    }

    // ─── Disabled rule does not contribute ────────────────────────────────

    void testDisabledRuleSkipped()
    {
        QList<PWR::Rule> rules;
        PWR::Rule pinned =
            CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{specific}"), QString(), 303);
        pinned.enabled = false; // disabled — must be skipped
        rules.append(pinned);
        rules.append(CRB::makeAssignmentRule(QStringLiteral("Default"), QString(), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{global}"), QString(), 1));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // The pinned rule is disabled → the query falls through to the default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{global}"));
    }

    // ─── Window-property predicates are inert for a windowless query ──────
    // The file docstring's core invariant: a rule that pins a window-property
    // field (AppId) cannot match a windowless context query, because the
    // absent AppId makes that leaf evaluate false — and inside an All{} a
    // single false child fails the whole rule. Without this the rule would
    // wrongly shadow the cascade.

    void testWindowPropertyRuleInertForWindowlessQuery()
    {
        QList<PWR::Rule> rules;
        // A context+AppId composite rule: ScreenId == DP-1 AND AppId == konsole.
        PWR::Rule composite;
        composite.id = QUuid::createUuid();
        composite.name = QStringLiteral("DP-1 konsole");
        composite.enabled = true;
        composite.priority = 999; // far above any cascade band — would win if it matched
        composite.match = PWR::MatchExpression::makeAll(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1")),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals,
                                            QStringLiteral("org.kde.konsole"))});
        composite.actions =
            CRB::makeAssignmentActions(QStringLiteral("snapping"), QStringLiteral("{window-rule-layout}"), QString());
        rules.append(composite);
        // A plain context rule for the same screen.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{context-layout}"), QString(),
                                             301));

        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // The windowless context query carries no AppId → the composite rule's
        // AppId leaf evaluates false → the All{} fails → the composite rule
        // does NOT match. The context-only rule resolves instead.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{context-layout}"));
    }

    // ─── Tie-break is stable list order ───────────────────────────────────
    // Two rules at the same priority — the evaluator's stable sort keeps the
    // first-listed rule's action filling the slot.

    void testTieBreakIsListOrder()
    {
        QList<PWR::Rule> rules;
        // Two screen-only rules for DP-1 — same priority (301).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("first"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{first}"), QString(), 301));
        rules.append(CRB::makeAssignmentRule(QStringLiteral("second"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{second}"), QString(), 301));
        PWR::RuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // First-listed rule wins the slot (first-action-per-slot, stable sort).
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{first}"));
    }

    // ─── LayoutRegistry boundary: mixed rules and the cascade ─────────────
    //
    // The model-level tests above prove the RuleEvaluator's resolve() walk
    // handles window-property predicates correctly for windowless queries.
    // The cases below close the symmetric proof at the LayoutRegistry
    // boundary — production code resolves through assignmentEntryForScreen,
    // which composes m_evaluator->highestPriorityMatch(query, filter) with a
    // filter that admits any rule carrying an engine-mode action and not
    // the catch-all. A mixed (context + window-property) rule passes that
    // structural filter, so the cascade-fidelity proof has to verify the
    // predicate-evaluation guard at resolve() time still keeps the mixed
    // rule out for the windowless queries the registry issues. These tests
    // assert that end-to-end, plus the provider-default synthesis on total
    // cascade miss.

    // ─── Case 1: mixed rule does not leak into context-only resolution ────
    //
    // Symmetric to testWindowPropertyRuleInertForWindowlessQuery, but
    // driven through LayoutRegistry::assignmentEntryForScreen — the entry
    // point production uses. A hand-authored mixed rule
    // (All{ScreenId == DP-2, AppId == firefox, SetEngineMode autotile})
    // passes the registry filter structurally (it carries an engine-mode
    // action and is not the catch-all), but its AppId leaf evaluates false
    // against the registry's windowless query and so the All{} must fail.
    // The registry has to fall through to the gated default — the mixed
    // rule's autotile mode must NOT surface.

    void testMixedRuleDoesNotLeakIntoContextOnlyResolution()
    {
        RegistryFixture f = makeRegistryFixture();
        // Snap default so the synthesised gated default is unambiguously
        // distinguishable from the mixed rule's autotile output.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        const PWR::Rule mixed = makeMixedScreenAppRule(QStringLiteral("DP-2"), QStringLiteral("firefox"),
                                                       /*autotileMode=*/true, /*snappingLayout=*/QString(),
                                                       /*tilingAlgorithm=*/QStringLiteral("dwindle"));
        QVERIFY(f.store->addRule(mixed));

        // The registry queries the rule set with a WINDOWLESS query — no
        // AppId — so the mixed rule's AppId leaf evaluates false; its All{}
        // fails; the cascade misses and the level-1 default synthesises.
        const PhosphorZones::AssignmentEntry entry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 1, QString());

        // The synthesised entry is Snapping (gated default), not the
        // Autotile/dwindle that would arrive if the mixed rule leaked.
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("{provider-snap-default}"));
        QVERIFY(entry.tilingAlgorithm.isEmpty());
    }

    // ─── Case 2: mixed rule inert across the field-level readers ──────────
    //
    // Same rule set shape, but exercised through the field-level readers
    // (modeForScreen, snappingLayoutForScreen, tilingAlgorithmForScreen)
    // that the OSD / cursor-move paths use. Each reader funnels through
    // assignmentEntryForScreen but exposes a different projection, so the
    // mixed rule's actions could in principle bleed through e.g.
    // tilingAlgorithmForScreen even if mode resolved correctly. This case
    // pins that none of the three field readers see the mixed rule.

    void testMixedRuleDoesNotLeakIntoWindowlessQuery()
    {
        RegistryFixture f = makeRegistryFixture();
        // No provider defaults configured → the field readers must observe
        // empty, confirming the mixed rule's actions never won any slot.
        const PWR::Rule mixed =
            makeMixedScreenAppRule(QStringLiteral("DP-2"), QStringLiteral("firefox"),
                                   /*autotileMode=*/false, /*snappingLayout=*/QStringLiteral("{mixed-snap}"),
                                   /*tilingAlgorithm=*/QStringLiteral("{mixed-tile}"));
        QVERIFY(f.store->addRule(mixed));

        const QString screen = QStringLiteral("DP-2");
        // No projection of the mixed rule surfaces — the cascade misses and
        // every default-less reader returns the cascade-miss empty value.
        // The mode defaults to Snapping (the AssignmentEntry default).
        QCOMPARE(f.registry->modeForScreen(screen, 1, QString()), PhosphorZones::AssignmentEntry::Snapping);
        QVERIFY(f.registry->snappingLayoutForScreen(screen, 1, QString()).isEmpty());
        QVERIFY(f.registry->tilingAlgorithmForScreen(screen, 1, QString()).isEmpty());
    }

    // ─── Case 3: context-only rule wins over a higher-priority mixed rule
    //
    // The registry filter does NOT order by priority — it filters on
    // structural shape (engine-mode action present, not catch-all) and lets
    // RuleEvaluator::highestPriorityMatch pick the winner. A pinned
    // context-only rule (priority 301) and a structurally-admitted but
    // predicate-failing mixed rule (priority 999) MUST resolve to the
    // context-only rule for a windowless query — the mixed rule's higher
    // numeric priority is moot because its predicate evaluates false.

    void testContextOnlyRuleAndMixedRulePreservePriority()
    {
        RegistryFixture f = makeRegistryFixture();
        // Context-only snapping rule at the screen-only band (priority 301).
        const PWR::Rule contextOnly =
            CRB::makeAssignmentRule(QStringLiteral("DP-3 default"), QStringLiteral("DP-3"), 0, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{context-only-snap}"), QString(), 301);
        // Mixed rule at far higher priority — but its AppId leaf gates a
        // windowless query out.
        const PWR::Rule mixed = makeMixedScreenAppRule(QStringLiteral("DP-3"), QStringLiteral("firefox"),
                                                       /*autotileMode=*/true, /*snappingLayout=*/QString(),
                                                       /*tilingAlgorithm=*/QStringLiteral("dwindle"), /*priority=*/999);
        // Insert mixed BEFORE the context-only rule so list order would
        // favour the mixed rule if structure alone won the resolution.
        QVERIFY(f.store->setAllRules({mixed, contextOnly}));

        const PhosphorZones::AssignmentEntry entry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-3"), 1, QString());

        // The context-only rule wins — the mixed rule is filtered by its
        // own predicate at resolve time, not by the registry's structural
        // filter. The snapping layout flows through; the mixed rule's
        // tilingAlgorithm does NOT.
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("{context-only-snap}"));
        QVERIFY(entry.tilingAlgorithm.isEmpty());
    }

    // ─── Case 4: provider-default synthesis when only mixed rules exist ───
    //
    // The most adversarial shape — a registry that holds ONLY mixed rules.
    // Every rule passes the structural filter (engine-mode action, not
    // catch-all), but every rule's window-property leaf gates the
    // windowless query out. resolveAssignmentEntry returns nullopt and
    // assignmentEntryForScreen has to synthesise from the level-1 provider
    // default. Two sub-cases — snap-default-only and autotile-default-only
    // — confirm both branches of resolveDefaultAssignmentEntry's three-tier
    // precedence are reached.

    void testProviderDefaultSynthesisWhenOnlyMixedRulesExist()
    {
        // ── Sub-case A: snap provider default ──
        {
            RegistryFixture f = makeRegistryFixture();
            f.registry->setDefaultLayoutIdProvider([]() {
                return QStringLiteral("{snap-default}");
            });
            // Two unrelated mixed rules — neither's window-property leaf
            // can match a windowless query.
            QVERIFY(f.store->addRule(makeMixedScreenAppRule(QStringLiteral("DP-1"), QStringLiteral("konsole"),
                                                            /*autotileMode=*/false, QStringLiteral("{ignored-konsole}"),
                                                            QString())));
            QVERIFY(
                f.store->addRule(makeMixedScreenAppRule(QStringLiteral("DP-1"), QStringLiteral("firefox"),
                                                        /*autotileMode=*/true, QString(), QStringLiteral("dwindle"))));

            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 1, QString());
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
            QCOMPARE(entry.snappingLayout, QStringLiteral("{snap-default}"));
            QVERIFY(entry.tilingAlgorithm.isEmpty());
        }

        // ── Sub-case B: autotile provider default (snap provider returns
        //                empty → autotile wins) ──
        {
            RegistryFixture f = makeRegistryFixture();
            f.registry->setDefaultLayoutIdProvider([]() {
                return QString();
            });
            f.registry->setDefaultAutotileAlgorithmProvider([]() {
                return QStringLiteral("bsp");
            });
            QVERIFY(f.store->addRule(makeMixedScreenAppRule(QStringLiteral("HDMI-1"), QStringLiteral("vlc"),
                                                            /*autotileMode=*/false, QStringLiteral("{ignored}"),
                                                            QString())));

            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("HDMI-1"), 1, QString());
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
            QVERIFY(entry.snappingLayout.isEmpty());
            QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
        }
    }

    // ─── User catch-all engine rule is the default floor ──────────────────
    //
    // A "Default / Any window" rule (catch-all match, engine=snapping,
    // layout=Grid 2x2) is just a normal rule whose match accepts every query.
    // At a LOW priority it is the floor: every context with no higher-priority
    // pin — including a freshly-switched virtual desktop — resolves to it, and
    // a pinned rule at a higher priority outranks it. When NO rule matches at
    // all the registry falls back to the settings-gated default resolver.

    void testUserCatchAllEngineRuleDrivesFloor()
    {
        // ── A: the catch-all floor drives a context with no pin ──
        {
            RegistryFixture f = makeRegistryFixture();
            // A global default DISTINCT from the rule's output, so a wrong
            // fallthrough would surface as Autotile/bsp instead of the rule.
            f.registry->setDefaultAutotileAlgorithmProvider([]() {
                return QStringLiteral("bsp");
            });
            QVERIFY(f.store->addRule(
                makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{grid-2x2}"), QString(), 1)));

            // Desktop 2, any screen — no pinned rule. The catch-all floor wins,
            // not the BSP global default.
            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 2, QString());
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
            QCOMPARE(entry.snappingLayout, QStringLiteral("{grid-2x2}"));
        }

        // ── B: a higher-priority pin beats the catch-all floor ──
        {
            RegistryFixture f = makeRegistryFixture();
            // Screen-only pin at the screen band (301) ...
            const PWR::Rule pinned =
                CRB::makeAssignmentRule(QStringLiteral("DP-5"), QStringLiteral("DP-5"), 0, QString(),
                                        QStringLiteral("snapping"), QStringLiteral("{screen-pin}"), QString(), 301);
            // ... versus the user catch-all floor at priority 1, well below it.
            const PWR::Rule catchAll =
                makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{grid-2x2}"), QString(), 1);
            // Insert the catch-all FIRST so list order would favour it; the pin
            // still wins on DP-5 because its priority outranks the floor.
            QVERIFY(f.store->setAllRules({catchAll, pinned}));

            // On DP-5 the pin (301) outranks the catch-all floor (1).
            QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-5"), 1, QString()).snappingLayout,
                     QStringLiteral("{screen-pin}"));
            // On any other screen the catch-all floor still applies.
            QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-6"), 1, QString()).snappingLayout,
                     QStringLiteral("{grid-2x2}"));
        }
    }

    // ─── Layout-only rule fills its slot without forcing the engine ───────
    //
    // A rule carrying ONLY a SetSnappingLayout (or SetTilingAlgorithm) action,
    // no SetEngineMode, sets the layout for that engine in the context but does
    // NOT force the engine mode. The mode comes from the default (or another
    // rule); the layout slot is filled independently. This is the per-slot
    // composition model — distinct from a rule that pins the engine.

    void testLayoutOnlyRuleFillsSlotWithoutForcingMode()
    {
        // ── A: the reported scenario — global default is snapping, a catch-all
        //       SetSnappingLayout rule supplies the snapping layout ──
        {
            RegistryFixture f = makeRegistryFixture();
            f.registry->setSnappingPreferredProvider([]() {
                return true; // default engine is snapping, but with no default layout id
            });
            // A layout-only catch-all: Columns(3), NO engine-mode action.
            PWR::Rule layoutOnly =
                makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{columns-3}"), QString(), 1);
            // Drop the engine-mode action makeUserCatchAllRule's helper does not
            // add (it builds via makeAssignmentActions with a snapping mode, so
            // strip the SetEngineMode to model the UI's layout-only rule).
            layoutOnly.actions.removeIf([](const PWR::RuleAction& a) {
                return a.type == QString(PWR::ActionType::SetEngineMode);
            });
            QVERIFY(f.store->addRule(layoutOnly));

            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 2, QString());
            // Mode is the default's snapping (not forced by the rule), and the
            // rule supplies the layout the default left empty.
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
            QCOMPARE(entry.snappingLayout, QStringLiteral("{columns-3}"));
        }

        // ── B: a layout-only rule does NOT flip the engine — default autotile
        //       stays autotile, the snapping layout is merely stored (lossless) ──
        {
            RegistryFixture f = makeRegistryFixture();
            f.registry->setDefaultAutotileAlgorithmProvider([]() {
                return QStringLiteral("bsp"); // default engine resolves to autotile
            });
            PWR::Rule layoutOnly =
                makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{columns-3}"), QString(), 1);
            layoutOnly.actions.removeIf([](const PWR::RuleAction& a) {
                return a.type == QString(PWR::ActionType::SetEngineMode);
            });
            QVERIFY(f.store->addRule(layoutOnly));

            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("HDMI-1"), 1, QString());
            // The rule did not force snapping — the engine stays the default's
            // autotile — but the snapping layout slot is filled for a later toggle.
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
            QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
            QCOMPARE(entry.snappingLayout, QStringLiteral("{columns-3}"));
        }

        // ── C: a pinned engine rule sets the mode; a separate catch-all
        //       layout-only rule fills the layout slot — they compose ──
        {
            RegistryFixture f = makeRegistryFixture();
            // Pinned engine-only rule (autotile, default algorithm) on DP-4.
            const PWR::Rule pinnedMode =
                CRB::makeAssignmentRule(QStringLiteral("DP-4 autotile"), QStringLiteral("DP-4"), 0, QString(),
                                        QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"), 301);
            // Catch-all layout-only snapping rule.
            PWR::Rule layoutOnly =
                makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{columns-3}"), QString(), 1);
            layoutOnly.actions.removeIf([](const PWR::RuleAction& a) {
                return a.type == QString(PWR::ActionType::SetEngineMode);
            });
            QVERIFY(f.store->setAllRules({layoutOnly, pinnedMode}));

            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-4"), 1, QString());
            // Engine from the pinned rule; snapping layout from the catch-all.
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
            QCOMPARE(entry.tilingAlgorithm, QStringLiteral("dwindle"));
            QCOMPARE(entry.snappingLayout, QStringLiteral("{columns-3}"));
        }
    }

    // ─── Default change is reflected without a rule-set mutation ──────────
    //
    // A layout-only rule bases its entry on the GLOBAL default for every slot
    // it does not fill. The resolver memoizes only the rule-derived portion, so
    // a default-setting change must surface immediately — with NO rule-set
    // revision bump (a settings edit produces none). This guards against baking
    // the live default into the revision-invalidated context cache, which would
    // pin a stale mode/algorithm on the layout-only path until the next rule
    // edit or daemon restart.

    void testDefaultChangeReflectedWithoutRuleMutation()
    {
        RegistryFixture f = makeRegistryFixture();
        bool snappingPreferred = false;
        f.registry->setSnappingPreferredProvider([&snappingPreferred]() {
            return snappingPreferred;
        });
        f.registry->setDefaultAutotileAlgorithmProvider([]() {
            return QStringLiteral("bsp");
        });

        // Layout-only catch-all: snapping layout, NO engine-mode action.
        PWR::Rule layoutOnly =
            makeUserCatchAllRule(/*autotileMode=*/false, QStringLiteral("{columns-3}"), QString(), 1);
        layoutOnly.actions.removeIf([](const PWR::RuleAction& a) {
            return a.type == QString(PWR::ActionType::SetEngineMode);
        });
        QVERIFY(f.store->addRule(layoutOnly));

        // Default engine is autotile (snapping not preferred). The rule fills
        // the snapping slot; the mode is the default's autotile.
        {
            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 2, QString());
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Autotile);
            QCOMPARE(entry.tilingAlgorithm, QStringLiteral("bsp"));
            QCOMPARE(entry.snappingLayout, QStringLiteral("{columns-3}"));
        }

        // Flip the global default to snapping-preferred WITHOUT mutating any
        // rule. The same cached context must now report the NEW default mode,
        // not the stale autotile one.
        snappingPreferred = true;
        {
            const PhosphorZones::AssignmentEntry entry =
                f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 2, QString());
            QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
            QCOMPARE(entry.snappingLayout, QStringLiteral("{columns-3}"));
        }
    }
};

QTEST_MAIN(TestRuleCascadeFidelity)
#include "test_rule_cascade_fidelity.moc"

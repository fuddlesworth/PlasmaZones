// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_windowrule_cascade_fidelity.cpp
 * @brief Cascade-fidelity proof for the window-rule context model.
 *
 * The zone-Assignment cascade (exact → activity → desktop → screen → provider
 * default) is reproduced in the WindowRule world by priority ordering: a rule
 * pinning more context dimensions gets a higher priority, and the
 * RuleEvaluator's descending-priority walk lands the most-specific match
 * first.
 *
 * This suite is the correctness proof of the @ref
 * PhosphorWindowRule::ContextRuleBridge priority formula. It ports the
 * behavioural scenarios from tests/unit/core/test_layoutmanager_assignment.cpp
 * — exact-match-wins, activity-beats-desktop, screen-only display default,
 * mode-only autotile entries, provider-default fallback — and asserts a
 * RuleEvaluator over the migration-built context rules resolves each
 * windowless context query to the same engine-mode / layout the legacy
 * cascade produced.
 *
 * SCOPE — this suite proves the rule MODEL is cascade-faithful: it exercises
 * the RuleEvaluator + ContextRuleBridge directly. LayoutRegistry's own
 * byte-identical re-implementation on this model is verified separately by
 * tests/unit/core/test_layoutmanager_assignment.cpp (the 42-assertion oracle,
 * which runs against the rule-backed registry). The connector-name /
 * virtual-screen fallback is a query-side retry loop in LayoutRegistry and is
 * out of this model-level suite's scope by design.
 */

#include <QString>
#include <QTest>

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>

namespace PWR = PhosphorWindowRule;
namespace CRB = PhosphorWindowRule::ContextRuleBridge;

class TestWindowRuleCascadeFidelity : public QObject
{
    Q_OBJECT

private:
    /// A windowless context query — only the context attributes are set, so
    /// only context-only rules contribute (window-property predicates would
    /// evaluate false). This reproduces the old Assignment cascade input.
    PWR::WindowQuery contextQuery(const QString& screenId, int desktop, const QString& activity)
    {
        PWR::WindowQuery q;
        q.screenId = screenId;
        q.virtualDesktop = desktop;
        q.activity = activity;
        return q;
    }

    /// The engine-mode token resolved for a query, or empty if the engine-mode
    /// slot was unfilled.
    QString resolvedMode(const PWR::RuleEvaluator& evaluator, const PWR::WindowQuery& q)
    {
        const PWR::ResolvedActions actions = evaluator.resolve(q);
        const auto slot = actions.slot(QString(PWR::ActionSlot::EngineMode));
        if (!slot) {
            return QString();
        }
        return slot->params.value(QLatin1String("mode")).toString();
    }

    /// The layout slot's layoutId / algorithm for a query.
    QString resolvedLayoutId(const PWR::RuleEvaluator& evaluator, const PWR::WindowQuery& q)
    {
        const PWR::ResolvedActions actions = evaluator.resolve(q);
        const auto slot = actions.slot(QString(PWR::ActionSlot::Layout));
        if (!slot) {
            return QString();
        }
        if (slot->type == QString(PWR::ActionType::SetSnappingLayout)) {
            return slot->params.value(QLatin1String("layoutId")).toString();
        }
        return slot->params.value(QLatin1String("algorithm")).toString();
    }

private Q_SLOTS:

    // ─── Priority formula — exact values ─────────────────────────────────

    void testPriorityFormula_exactValues()
    {
        // exact = screen + desktop + activity
        QCOMPARE(CRB::contextPriority(true, true, true), 610);
        // screen + activity
        QCOMPARE(CRB::contextPriority(true, false, true), 510);
        // screen + desktop
        QCOMPARE(CRB::contextPriority(true, true, false), 410);
        // screen only
        QCOMPARE(CRB::contextPriority(true, false, false), 310);
        // provider default — pins nothing
        QCOMPARE(CRB::contextPriority(false, false, false), 0);
        // activity-pinned strictly outranks desktop-pinned (structural)
        QVERIFY(CRB::contextPriority(true, false, true) > CRB::contextPriority(true, true, false));
    }

    // ─── Exact match wins ─────────────────────────────────────────────────
    // Ports testLayoutManager_layoutForScreen_fallbackCascade.

    void testExactMatchWins()
    {
        QList<PWR::WindowRule> rules;
        // Display default for DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             /*autotile=*/false, QStringLiteral("{screen-layout}"), QString()));
        // Desktop 2 on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             /*autotile=*/false, QStringLiteral("{desktop-layout}"), QString()));
        rules.append(
            CRB::makeProviderDefaultRule(QStringLiteral("Default"), false, QStringLiteral("{global}"), QString()));

        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // Desktop 2 → desktop-specific layout.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{desktop-layout}"));
        // Desktop 1 has no explicit entry → cascades to the DP-1 display default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{screen-layout}"));
        // A different screen → provider default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("HDMI-1"), 1, QString())),
                 QStringLiteral("{global}"));
    }

    // ─── Activity beats desktop ───────────────────────────────────────────
    // Ports testLayoutManager_layoutForScreen_activityWinsOverDesktop.

    void testActivityWinsOverDesktop()
    {
        QList<PWR::WindowRule> rules;
        // Desktop 2 on DP-1 (priority 410).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(), false,
                                             QStringLiteral("{desktop-two}"), QString()));
        // Activity "work" on DP-1, any desktop (priority 510).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("work-uuid"), false, QStringLiteral("{activity-work}"),
                                             QString()));

        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // In the work activity on desktop 2 → activity rule wins (510 > 410).
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
        QList<PWR::WindowRule> rules;
        // Monitor default for DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(), false,
                                             QStringLiteral("{monitor-default}"), QString()));
        // Work activity on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("activity-work"), false, QStringLiteral("{work-activity}"),
                                             QString()));

        PWR::WindowRuleSet set;
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
        QList<PWR::WindowRule> rules;
        // Autotile with both a snapping layout AND a tiling algorithm (the
        // mode-toggle-lossless shape).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             /*autotile=*/true, QStringLiteral("{snap-preserved}"),
                                             QStringLiteral("dwindle")));

        PWR::WindowRuleSet set;
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
    }

    // ─── Mode-only autotile entry ─────────────────────────────────────────
    // Ports testAssignmentEntry_modeOnlyAutotile_cascadeAccepts: a KCM
    // "autotile, default algorithm" entry has both layout fields empty and
    // therefore migrates to a rule with ONLY a setEngineMode action.

    void testModeOnlyAutotileEntry()
    {
        QList<PWR::WindowRule> rules;
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             /*autotile=*/true, QString(), QString()));
        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        const PWR::WindowQuery q = contextQuery(QStringLiteral("DP-1"), 1, QString());
        // The mode is still resolved — the engine-mode slot is filled.
        QCOMPARE(resolvedMode(evaluator, q), QStringLiteral("autotile"));
        // No layout slot — the entry was mode-only.
        QVERIFY(!evaluator.resolve(q).hasSlot(QString(PWR::ActionSlot::Layout)));
    }

    // ─── Provider-default fallback ────────────────────────────────────────
    // Ports testLevel1Default_* — a context with no pinned rule resolves to
    // the priority-0 catch-all.

    void testProviderDefaultFallback()
    {
        QList<PWR::WindowRule> rules;
        // One pinned rule for DP-1 only.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(), false,
                                             QStringLiteral("{dp1-layout}"), QString()));
        // Autotile provider default.
        rules.append(CRB::makeProviderDefaultRule(QStringLiteral("Default"), /*autotile=*/true, QString(),
                                                  QStringLiteral("bsp")));

        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // DP-1 hits the pinned rule.
        QCOMPARE(resolvedMode(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("snapping"));
        // HDMI-9 has no pinned rule → provider default (autotile, bsp).
        const PWR::WindowQuery other = contextQuery(QStringLiteral("HDMI-9"), 3, QString());
        QCOMPARE(resolvedMode(evaluator, other), QStringLiteral("autotile"));
        QCOMPARE(resolvedLayoutId(evaluator, other), QStringLiteral("bsp"));
    }

    // ─── Provider default never shadows a pinned rule ─────────────────────
    // The catch-all matches every query, but its priority-0 ranking means a
    // pinned rule (priority >= 310) always resolves first.

    void testProviderDefaultNeverShadowsPinned()
    {
        QList<PWR::WindowRule> rules;
        rules.append(
            CRB::makeProviderDefaultRule(QStringLiteral("Default"), false, QStringLiteral("{global}"), QString()));
        // Add the pinned rule AFTER the default so list order would favour the
        // default if priority were ignored — priority must still win.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(), false,
                                             QStringLiteral("{specific}"), QString()));

        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{specific}"));
    }

    // ─── Disabled rule does not contribute ────────────────────────────────

    void testDisabledRuleSkipped()
    {
        QList<PWR::WindowRule> rules;
        PWR::WindowRule pinned = CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2,
                                                         QString(), false, QStringLiteral("{specific}"), QString());
        pinned.enabled = false; // disabled — must be skipped
        rules.append(pinned);
        rules.append(
            CRB::makeProviderDefaultRule(QStringLiteral("Default"), false, QStringLiteral("{global}"), QString()));

        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // The pinned rule is disabled → the query falls through to the default.
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 2, QString())),
                 QStringLiteral("{global}"));
    }

    // ─── Tie-break is stable list order ───────────────────────────────────
    // Two rules at the same priority — the evaluator's stable sort keeps the
    // first-listed rule's action filling the slot.

    void testTieBreakIsListOrder()
    {
        QList<PWR::WindowRule> rules;
        // Two screen-only rules for DP-1 — same priority (310).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("first"), QStringLiteral("DP-1"), 0, QString(), false,
                                             QStringLiteral("{first}"), QString()));
        rules.append(CRB::makeAssignmentRule(QStringLiteral("second"), QStringLiteral("DP-1"), 0, QString(), false,
                                             QStringLiteral("{second}"), QString()));
        PWR::WindowRuleSet set;
        set.setRules(rules);
        PWR::RuleEvaluator evaluator(set);

        // First-listed rule wins the slot (first-action-per-slot, stable sort).
        QCOMPARE(resolvedLayoutId(evaluator, contextQuery(QStringLiteral("DP-1"), 1, QString())),
                 QStringLiteral("{first}"));
    }
};

QTEST_MAIN(TestWindowRuleCascadeFidelity)
#include "test_windowrule_cascade_fidelity.moc"

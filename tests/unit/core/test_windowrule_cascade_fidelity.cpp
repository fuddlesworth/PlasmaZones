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
 * still be inert against the windowless queries the registry issues.
 */

#include <QDir>
#include <QScopedPointer>
#include <QString>
#include <QTest>
#include <memory>
#include <vector>

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRule.h>
#include <PhosphorWindowRule/WindowRuleSet.h>
#include <PhosphorWindowRule/WindowRuleStore.h>

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "../helpers/IsolatedConfigGuard.h"
#include "config/configdefaults.h"
#include "config/configkeys.h"

namespace PWR = PhosphorWindowRule;
namespace CRB = PhosphorWindowRule::ContextRuleBridge;

using PlasmaZones::ConfigDefaults;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

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
        return slot->params.value(PWR::ActionParam::Mode).toString();
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
            return slot->params.value(PWR::ActionParam::LayoutId).toString();
        }
        return slot->params.value(PWR::ActionParam::Algorithm).toString();
    }

    /// Per-test fixture for the LayoutRegistry-boundary cases: an isolated XDG
    /// tempdir, a freshly-constructed WindowRuleStore pointed at the isolated
    /// rules file, and a LayoutRegistry that borrows the store. Kept here
    /// (rather than shared via LayoutRegistryTestHelpers.h) because these
    /// cases need to inject hand-crafted mixed rules straight into the store;
    /// the shared helper hides the store pointer.
    struct RegistryFixture
    {
        std::unique_ptr<IsolatedConfigGuard> guard;
        std::unique_ptr<PWR::WindowRuleStore> store;
        std::unique_ptr<PhosphorZones::LayoutRegistry> registry;
    };

    RegistryFixture makeRegistryFixture()
    {
        RegistryFixture f;
        f.guard = std::make_unique<IsolatedConfigGuard>();
        f.store = std::make_unique<PWR::WindowRuleStore>(ConfigDefaults::windowRulesFilePath());
        f.registry =
            std::make_unique<PhosphorZones::LayoutRegistry>(f.store.get(), PlasmaZones::ConfigKeys::layoutsSubdir());
        const QString layoutDir = f.guard->dataPath() + QLatin1Char('/') + PlasmaZones::ConfigKeys::layoutsSubdir();
        QDir().mkpath(layoutDir);
        f.registry->setLayoutDirectory(layoutDir);
        return f;
    }

    /// Build a mixed (context + window-property) rule at @p priority. The
    /// shape is All{ScreenId == screenId, AppId == appId}: it passes the
    /// LayoutRegistry filter (engine-mode action present, not catch-all) but
    /// its AppId leaf evaluates false against a windowless query, so the
    /// All{} must fail at resolve() time. Far above any pinned-context
    /// priority band so any leak would surface as this rule's layout
    /// winning the resolution.
    PWR::WindowRule makeMixedScreenAppRule(const QString& screenId, const QString& appId, bool autotileMode,
                                           const QString& snappingLayout, const QString& tilingAlgorithm,
                                           int priority = 999)
    {
        PWR::WindowRule mixed;
        mixed.id = QUuid::createUuid();
        mixed.name = screenId + QLatin1Char(' ') + appId;
        mixed.enabled = true;
        mixed.priority = priority;
        mixed.match = PWR::MatchExpression::makeAll(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, appId)});
        mixed.actions = CRB::makeAssignmentActions(
            autotileMode ? QStringLiteral("autotile") : QStringLiteral("snapping"), snappingLayout, tilingAlgorithm);
        return mixed;
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
                                             QStringLiteral("snapping"), QStringLiteral("{screen-layout}"), QString()));
        // Desktop 2 on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{desktop-layout}"),
                                             QString()));
        rules.append(CRB::makeProviderDefaultRule(QStringLiteral("Default"), QStringLiteral("snapping"),
                                                  QStringLiteral("{global}"), QString()));

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
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{desktop-two}"), QString()));
        // Activity "work" on DP-1, any desktop (priority 510).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("work-uuid"), QStringLiteral("snapping"),
                                             QStringLiteral("{activity-work}"), QString()));

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
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{monitor-default}"),
                                             QString()));
        // Work activity on DP-1.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 work"), QStringLiteral("DP-1"), 0,
                                             QStringLiteral("activity-work"), QStringLiteral("snapping"),
                                             QStringLiteral("{work-activity}"), QString()));

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
                                             QStringLiteral("autotile"), QStringLiteral("{snap-preserved}"),
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
        QList<PWR::WindowRule> rules;
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("autotile"), QString(), QString()));
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
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{dp1-layout}"), QString()));
        // Autotile provider default.
        rules.append(CRB::makeProviderDefaultRule(QStringLiteral("Default"), QStringLiteral("autotile"), QString(),
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
        rules.append(CRB::makeProviderDefaultRule(QStringLiteral("Default"), QStringLiteral("snapping"),
                                                  QStringLiteral("{global}"), QString()));
        // Add the pinned rule AFTER the default so list order would favour the
        // default if priority were ignored — priority must still win.
        rules.append(CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{specific}"), QString()));

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
        PWR::WindowRule pinned =
            CRB::makeAssignmentRule(QStringLiteral("DP-1 d2"), QStringLiteral("DP-1"), 2, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{specific}"), QString());
        pinned.enabled = false; // disabled — must be skipped
        rules.append(pinned);
        rules.append(CRB::makeProviderDefaultRule(QStringLiteral("Default"), QStringLiteral("snapping"),
                                                  QStringLiteral("{global}"), QString()));

        PWR::WindowRuleSet set;
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
        QList<PWR::WindowRule> rules;
        // A context+AppId composite rule: ScreenId == DP-1 AND AppId == konsole.
        PWR::WindowRule composite;
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
                                             QStringLiteral("snapping"), QStringLiteral("{context-layout}"),
                                             QString()));

        PWR::WindowRuleSet set;
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
        QList<PWR::WindowRule> rules;
        // Two screen-only rules for DP-1 — same priority (310).
        rules.append(CRB::makeAssignmentRule(QStringLiteral("first"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{first}"), QString()));
        rules.append(CRB::makeAssignmentRule(QStringLiteral("second"), QStringLiteral("DP-1"), 0, QString(),
                                             QStringLiteral("snapping"), QStringLiteral("{second}"), QString()));
        PWR::WindowRuleSet set;
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
    // The registry has to fall through to the provider default — the mixed
    // rule's autotile mode must NOT surface.

    void testMixedRuleDoesNotLeakIntoContextOnlyResolution()
    {
        RegistryFixture f = makeRegistryFixture();
        // Snap default so the synthesised provider default is unambiguously
        // distinguishable from the mixed rule's autotile output.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        const PWR::WindowRule mixed = makeMixedScreenAppRule(QStringLiteral("DP-2"), QStringLiteral("firefox"),
                                                             /*autotileMode=*/true, /*snappingLayout=*/QString(),
                                                             /*tilingAlgorithm=*/QStringLiteral("dwindle"));
        QVERIFY(f.store->addRule(mixed));

        // The registry queries the rule set with a WINDOWLESS query — no
        // AppId — so the mixed rule's AppId leaf evaluates false; its All{}
        // fails; the cascade misses and the level-1 default synthesises.
        const PhosphorZones::AssignmentEntry entry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 1, QString());

        // The synthesised entry is Snapping (provider default), not the
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
        const PWR::WindowRule mixed =
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
    // context-only rule (priority 310) and a structurally-admitted but
    // predicate-failing mixed rule (priority 999) MUST resolve to the
    // context-only rule for a windowless query — the mixed rule's higher
    // numeric priority is moot because its predicate evaluates false.

    void testContextOnlyRuleAndMixedRulePreservePriority()
    {
        RegistryFixture f = makeRegistryFixture();
        // Context-only snapping rule at the screen-only band (priority 310).
        const PWR::WindowRule contextOnly =
            CRB::makeAssignmentRule(QStringLiteral("DP-3 default"), QStringLiteral("DP-3"), 0, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{context-only-snap}"), QString());
        // Mixed rule at far higher priority — but its AppId leaf gates a
        // windowless query out.
        const PWR::WindowRule mixed =
            makeMixedScreenAppRule(QStringLiteral("DP-3"), QStringLiteral("firefox"),
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

    // ─── Context-rule gap overrides ──────────────────────────────────────
    // Gaps are context-domain but, unlike engine-mode assignments, resolve
    // PER SLOT — so a zone-padding rule and a separate outer-gap rule on the
    // same context BOTH apply, and there is no engine-mode gate.

    void testContextGaps_perSlotComposition()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };
        const auto gapRule = [](const QString& name, int priority, const QString& screenId,
                                const QList<PWR::RuleAction>& actions) {
            PWR::WindowRule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // Higher-priority rule sets ONLY zone padding; lower-priority rule sets
        // ONLY the outer gap. Different slots → both must apply (no shadowing),
        // and neither carries an engine-mode action.
        const PWR::WindowRule pad = gapRule(QStringLiteral("pad"), 400, QStringLiteral("DP-1"),
                                            {intGapAction(PWR::ActionType::SetZonePadding, 0)});
        const PWR::WindowRule gap = gapRule(QStringLiteral("gap"), 300, QStringLiteral("DP-1"),
                                            {intGapAction(PWR::ActionType::SetOuterGap, 12)});
        QVERIFY(f.store->setAllRules({pad, gap}));

        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.zonePadding.has_value());
        QCOMPARE(*resolved.zonePadding, 0);
        QVERIFY(resolved.outerGap.has_value()); // separate slot — composes, not shadowed
        QCOMPARE(*resolved.outerGap, 12);
        QVERIFY(!resolved.usePerSideOuterGap.has_value());

        // A context the rules do not pin → no override (cascade falls through).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── Context lock resolution (ActionSlot::Locked) ─────────────────────
    // resolveContextLocked reads the boolean Locked slot off a matching
    // context rule. Mode-agnostic, never persisted — the daemon's
    // isContextLocked ORs it over the manual lock store.

    void testContextLock_resolution()
    {
        const auto lockRule = [](const QString& name, PWR::Field field, const QVariant& value, bool locked) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::LockContext);
            a.params.insert(QString(PWR::ActionParam::Value), locked);
            PWR::WindowRule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(field, PWR::Operator::Equals, value);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // A monitor lock (value = true) on DP-1, and an activity lock scoped to
        // "work". An explicit value = false lock on DP-3 must NOT lock.
        const PWR::WindowRule lockMonitor =
            lockRule(QStringLiteral("lock DP-1"), PWR::Field::ScreenId, QStringLiteral("DP-1"), true);
        const PWR::WindowRule lockActivity =
            lockRule(QStringLiteral("lock work"), PWR::Field::Activity, QStringLiteral("work-uuid"), true);
        const PWR::WindowRule unlockMonitor =
            lockRule(QStringLiteral("unlock DP-3"), PWR::Field::ScreenId, QStringLiteral("DP-3"), false);
        // A desktop-scoped lock: fires on virtual desktop 2 regardless of
        // screen/activity, proving the desktop axis of the context match.
        const PWR::WindowRule lockDesktop =
            lockRule(QStringLiteral("lock desktop 2"), PWR::Field::VirtualDesktop, 2, true);
        QVERIFY(f.store->setAllRules({lockMonitor, lockActivity, unlockMonitor, lockDesktop}));

        // DP-1 is locked regardless of desktop/activity (screen-only match).
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-1"), 3, QStringLiteral("anything")));
        // The activity lock fires only inside "work".
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-2"), 0, QStringLiteral("work-uuid")));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-2"), 0, QStringLiteral("play-uuid")));
        // The desktop lock fires on desktop 2 (any screen, no activity) and
        // not on other desktops.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 2, QString()));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 1, QString()));
        // value = false resolves to not-locked (explicit no-op overlay).
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-3"), 0, QString()));
        // A context no rule pins → not locked.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("HDMI-1"), 0, QString()));

        // Disabling the lock rule drops the lock (revision-invalidated cache):
        // DP-1 was primed locked above, so a stale cache would keep returning
        // true here — the post-mutation false proves the revision bump evicts.
        PWR::WindowRule disabled = lockMonitor;
        disabled.enabled = false;
        QVERIFY(f.store->setAllRules({disabled, lockActivity, unlockMonitor, lockDesktop}));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
    }

    // ─── Context lock — slot conflict resolution ──────────────────────────
    // When two LockContext rules pin the SAME context with opposing values,
    // the single Locked slot is won by the highest-priority rule (then list
    // order on a tie), exactly like the layout slot (testTieBreakIsListOrder).
    // The value itself does not bias the contest — proven by running it both
    // directions so neither "true always wins" nor "false always wins" passes.

    void testContextLock_priorityResolution()
    {
        const auto lockRuleAt = [](const QString& name, const QString& screenId, bool locked, int priority) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::LockContext);
            a.params.insert(QString(PWR::ActionParam::Value), locked);
            PWR::WindowRule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = {a};
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // DP-9: higher-priority rule says NOT locked → wins over a lower one
        // that says locked.
        const PWR::WindowRule dp9High = lockRuleAt(QStringLiteral("dp9 unlock"), QStringLiteral("DP-9"), false, 500);
        const PWR::WindowRule dp9Low = lockRuleAt(QStringLiteral("dp9 lock"), QStringLiteral("DP-9"), true, 400);
        // DP-10: the inverse — higher-priority rule says locked → wins.
        const PWR::WindowRule dp10High = lockRuleAt(QStringLiteral("dp10 lock"), QStringLiteral("DP-10"), true, 500);
        const PWR::WindowRule dp10Low = lockRuleAt(QStringLiteral("dp10 unlock"), QStringLiteral("DP-10"), false, 400);
        // DP-11: equal priority, lock=true first — first-listed rule wins.
        const PWR::WindowRule dp11First = lockRuleAt(QStringLiteral("dp11 a"), QStringLiteral("DP-11"), true, 400);
        const PWR::WindowRule dp11Second = lockRuleAt(QStringLiteral("dp11 b"), QStringLiteral("DP-11"), false, 400);
        // DP-12: the inverse tie-break — lock=false first at the same priority.
        // Run both directions so the tie-break is proven to be list-order, not
        // value-bias: a "true always wins on a tie" bug would pass DP-11 alone.
        const PWR::WindowRule dp12First = lockRuleAt(QStringLiteral("dp12 a"), QStringLiteral("DP-12"), false, 400);
        const PWR::WindowRule dp12Second = lockRuleAt(QStringLiteral("dp12 b"), QStringLiteral("DP-12"), true, 400);
        QVERIFY(
            f.store->setAllRules({dp9High, dp9Low, dp10High, dp10Low, dp11First, dp11Second, dp12First, dp12Second}));

        // Highest priority wins regardless of the value it carries.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-9"), 0, QString()));
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-10"), 0, QString()));
        // Equal priority → first-listed wins, in both directions.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-11"), 0, QString()));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-12"), 0, QString()));
    }

    // ─── Context lock composes with a layout/engine assignment ────────────
    // LockContext is terminal=false and fills the dedicated Locked slot, so a
    // lock-only rule must co-exist with a separate context-assignment rule on
    // the SAME context: the lock surfaces via resolveContextLocked AND the
    // layout still surfaces via assignmentEntryForScreen. Neither slot shadows
    // the other — the whole reason the action is non-terminal.

    void testContextLock_composesWithAssignment()
    {
        RegistryFixture f = makeRegistryFixture();

        // A lock-only rule (Locked slot) and a separate layout-assignment rule
        // (engine/layout slots), both pinned to DP-7 (screen-only match).
        PWR::RuleAction lockAction;
        lockAction.type = QString(PWR::ActionType::LockContext);
        lockAction.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::WindowRule lock;
        lock.id = QUuid::createUuid();
        lock.name = QStringLiteral("lock DP-7");
        lock.enabled = true;
        lock.priority = 400;
        lock.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-7"));
        lock.actions = {lockAction};

        const PWR::WindowRule assign =
            CRB::makeAssignmentRule(QStringLiteral("layout DP-7"), QStringLiteral("DP-7"), 0, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{ctx-layout}"), QString());
        QVERIFY(f.store->setAllRules({lock, assign}));

        // The lock surfaces (Locked slot) ...
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("DP-7"), 1, QString()));
        // ... and the layout assignment still surfaces (engine/layout slots),
        // unshadowed by the non-terminal lock rule.
        const PhosphorZones::AssignmentEntry entry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-7"), 1, QString());
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QStringLiteral("{ctx-layout}"));
    }
};

QTEST_MAIN(TestWindowRuleCascadeFidelity)
#include "test_windowrule_cascade_fidelity.moc"

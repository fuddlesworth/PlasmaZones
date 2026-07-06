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
 */

#include <QDir>
#include <QScopedPointer>
#include <QString>
#include <QTest>
#include <QUuid>
#include <limits>
#include <memory>
#include <vector>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include <PhosphorRules/RuleStore.h>

#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "../helpers/IsolatedConfigGuard.h"
#include "config/configdefaults.h"
#include "config/configkeys.h"

namespace PWR = PhosphorRules;
namespace CRB = PhosphorRules::ContextRuleBridge;

using PlasmaZones::ConfigDefaults;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestRuleCascadeFidelity : public QObject
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
    /// tempdir, a freshly-constructed RuleStore pointed at the isolated
    /// rules file, and a LayoutRegistry that borrows the store. Kept here
    /// (rather than shared via LayoutRegistryTestHelpers.h) because these
    /// cases need to inject hand-crafted mixed rules straight into the store;
    /// the shared helper hides the store pointer.
    struct RegistryFixture
    {
        std::unique_ptr<IsolatedConfigGuard> guard;
        std::unique_ptr<PWR::RuleStore> store;
        std::unique_ptr<PhosphorZones::LayoutRegistry> registry;
    };

    RegistryFixture makeRegistryFixture()
    {
        RegistryFixture f;
        f.guard = std::make_unique<IsolatedConfigGuard>();
        f.store = std::make_unique<PWR::RuleStore>(ConfigDefaults::rulesFilePath());
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
    PWR::Rule makeMixedScreenAppRule(const QString& screenId, const QString& appId, bool autotileMode,
                                     const QString& snappingLayout, const QString& tilingAlgorithm, int priority = 999)
    {
        PWR::Rule mixed;
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

    /// A USER-authored catch-all engine rule — an empty-All{} match (so the
    /// match accepts every query) at an explicit priority. This is the shape
    /// the Settings UI persists for a "Default / Any window" engine+layout rule.
    /// It is just a normal rule whose match accepts every query, so at a LOW
    /// priority it acts as the floor any pinned rule outranks.
    PWR::Rule makeUserCatchAllRule(bool autotileMode, const QString& snappingLayout, const QString& tilingAlgorithm,
                                   int priority)
    {
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("Default");
        r.enabled = true;
        r.priority = priority;
        r.match = PWR::MatchExpression(); // default-constructed → empty All{} catch-all
        r.actions = CRB::makeAssignmentActions(autotileMode ? QStringLiteral("autotile") : QStringLiteral("snapping"),
                                               snappingLayout, tilingAlgorithm);
        return r;
    }

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
            PWR::Rule r;
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
        const PWR::Rule pad = gapRule(QStringLiteral("pad"), 400, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetInnerGap, 0)});
        const PWR::Rule gap = gapRule(QStringLiteral("gap"), 300, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetOuterGap, 12)});
        QVERIFY(f.store->setAllRules({pad, gap}));

        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.innerGap.has_value());
        QCOMPARE(*resolved.innerGap, 0);
        QVERIFY(resolved.outerGap.has_value()); // separate slot — composes, not shadowed
        QCOMPARE(*resolved.outerGap, 12);
        QVERIFY(!resolved.usePerSideOuterGap.has_value());

        // A context the rules do not pin → no override (cascade falls through).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── ScreenOrientation is stamped onto context queries and gates rules ────
    // The orientation provider feeds "portrait" / "landscape" per screen; a rule
    // matching Field::ScreenOrientation must fire only on the screens the provider
    // reports that orientation for, proving the stamp reaches a non-assignment
    // (gap) resolver.
    void testContextOrientation_stampedAndGatesRule()
    {
        RegistryFixture f = makeRegistryFixture();
        // DP-1 is portrait, DP-2 is landscape; DP-3 has unknown geometry (nullopt).
        f.registry->setScreenOrientationProvider([](const QString& screenId) -> std::optional<QString> {
            if (screenId == QLatin1String("DP-1")) {
                return QStringLiteral("portrait");
            }
            if (screenId == QLatin1String("DP-2")) {
                return QStringLiteral("landscape");
            }
            return std::nullopt;
        });

        PWR::RuleAction gapAction;
        gapAction.type = QString(PWR::ActionType::SetInnerGap);
        gapAction.params.insert(QString(PWR::ActionParam::Value), 20);
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("portrait gap");
        r.enabled = true;
        r.priority = 400;
        r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenOrientation, PWR::Operator::Equals,
                                                 QStringLiteral("portrait"));
        r.actions = {gapAction};
        QVERIFY(f.store->setAllRules({r}));

        // Portrait screen → the orientation stamp matches → gap applies.
        const PhosphorZones::ContextGapOverride portrait =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(portrait.innerGap.has_value());
        QCOMPARE(*portrait.innerGap, 20);

        // Landscape screen → orientation token differs → rule inert.
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
        // Unknown geometry → orientation empty → rule inert (no false match).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-3"), 0, QString()).isEmpty());
    }

    // ─── ActiveLayout is stamped onto the non-assignment resolvers, gates rules,
    //     and does NOT recurse ──────────────────────────────────────────────────
    // The gap/lock/overlay resolvers stamp the screen's resolved active-layout id
    // (via assignmentIdForScreen). A rule matching Field::ActiveLayout must fire
    // only when that id matches — and the resolver must NOT recurse (reaching
    // assignmentIdForScreen must never re-enter the gap resolver). The test
    // completing at all proves the no-recursion contract.
    void testContextActiveLayout_stampedAndGatesRule()
    {
        RegistryFixture f = makeRegistryFixture();
        // No per-screen assignment rule; the active layout comes from the global
        // default provider (exercising the non-rule-set input path that the cache
        // key must fold in).
        const QString layoutId = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        f.registry->setDefaultLayoutIdProvider([layoutId]() {
            return layoutId;
        });

        const auto gapRuleForLayout = [](const QString& id) {
            PWR::RuleAction gapAction;
            gapAction.type = QString(PWR::ActionType::SetInnerGap);
            gapAction.params.insert(QString(PWR::ActionParam::Value), 15);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("active-layout gap");
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, id);
            r.actions = {gapAction};
            return r;
        };

        // The screen's active layout (from the default provider) matches → gap fires.
        QVERIFY(f.store->setAllRules({gapRuleForLayout(layoutId)}));
        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.innerGap.has_value());
        QCOMPARE(*resolved.innerGap, 15);

        // A rule pinned to a DIFFERENT layout id is inert on this screen.
        QVERIFY(f.store->setAllRules({gapRuleForLayout(QStringLiteral("{22222222-2222-2222-2222-222222222222}"))}));
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString()).isEmpty());
    }

    // ─── Context autotile-parameter resolution (max / split / master) ────────
    // resolveContextTilingParams is a per-slot read: independent
    // SetMaxWindows / SetSplitRatio / SetMasterCount rules compose, and an
    // unpinned screen resolves to an all-unset (empty) params struct.
    void testContextTilingParams_perSlotComposition()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        const auto tilingRule = [&](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };

        RegistryFixture f = makeRegistryFixture();
        // Separate rules fill separate slots — all compose (per-slot read).
        const PWR::Rule mw = tilingRule(QStringLiteral("mw"), 400, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetMaxWindows, 3)});
        const PWR::Rule sr = tilingRule(QStringLiteral("sr"), 300, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetSplitRatio, 0.6)});
        const PWR::Rule mc = tilingRule(QStringLiteral("mc"), 200, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetMasterCount, 2)});
        // Insert position carries a wire token → resolves to the AutotileInsertPosition int.
        const PWR::Rule ip =
            tilingRule(QStringLiteral("ip"), 100, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetInsertPosition, QString(PWR::InsertPositionToken::AsMaster))});
        // Overflow behavior carries a wire token → AutotileOverflowBehavior int.
        const PWR::Rule ob = tilingRule(
            QStringLiteral("ob"), 50, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetOverflowBehavior, QString(PWR::OverflowBehaviorToken::Unlimited))});
        // Drag behavior carries a wire token → AutotileDragBehavior int.
        const PWR::Rule db =
            tilingRule(QStringLiteral("db"), 25, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetDragBehavior, QString(PWR::DragBehaviorToken::Reorder))});
        // SetAlgorithmParam carries a target algorithm token + a free-form params blob.
        PWR::RuleAction apAction;
        apAction.type = QString(PWR::ActionType::SetAlgorithmParam);
        apAction.params.insert(QString(PWR::ActionParam::Algorithm), QStringLiteral("bsp"));
        QJsonObject apParams;
        apParams.insert(QStringLiteral("ratio"), 0.7);
        apAction.params.insert(QString(PWR::ActionParam::Params), apParams);
        const PWR::Rule ap = tilingRule(QStringLiteral("ap"), 10, QStringLiteral("DP-1"), {apAction});
        QVERIFY(f.store->setAllRules({mw, sr, mc, ip, ob, db, ap}));

        const PhosphorZones::ContextTilingParams p =
            f.registry->resolveContextTilingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(p.maxWindows.has_value());
        QCOMPARE(*p.maxWindows, 3);
        QVERIFY(p.splitRatio.has_value());
        QCOMPARE(*p.splitRatio, 0.6);
        QVERIFY(p.masterCount.has_value());
        QCOMPARE(*p.masterCount, 2);
        QVERIFY(p.insertPosition.has_value());
        QCOMPARE(*p.insertPosition, 2); // "asMaster" → AutotileInsertPosition::AsMaster (2)
        QVERIFY(p.overflowBehavior.has_value());
        QCOMPARE(*p.overflowBehavior, 1); // "unlimited" → AutotileOverflowBehavior::Unlimited (1)
        QVERIFY(p.dragBehavior.has_value());
        QCOMPARE(*p.dragBehavior, 1); // "reorder" → AutotileDragBehavior::Reorder (1)
        QCOMPARE(p.algorithmParamTarget, QStringLiteral("bsp"));
        QCOMPARE(p.algorithmParams.value(QStringLiteral("ratio")).toDouble(), 0.7);

        // A screen the rules do not pin → all-unset (the daemon then leaves the
        // config-derived override map untouched for that screen).
        QVERIFY(f.registry->resolveContextTilingParams(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── Per-monitor gap rule overrides the baseline for that screen only ────
    // A per-monitor gap override is authored by the Appearance page as a NORMAL
    // (non-managed) screen-scoped rule: match `ScreenId == screen`, carrying the
    // gap actions. It must override the GLOBAL default (the managed, catch-all
    // baseline rule that holds the global gaps) for that monitor only — and the
    // managed catch-all baseline must itself stay EXCLUDED from the context
    // override, so an un-pinned monitor reports no override and falls through to
    // the global default tier.

    void testContextGaps_perScreenRuleOverridesBaseline()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();

        // The managed, catch-all baseline rule that carries the GLOBAL default
        // gaps (inner = 4). It is pinned to lowest precedence and is the level-4
        // default tier, NOT a context override — resolveContextGaps excludes it.
        PWR::Rule baseline;
        baseline.id = ConfigDefaults::baselineGapRuleId();
        baseline.name = QStringLiteral("Default gaps");
        baseline.enabled = true;
        baseline.managed = true;
        baseline.priority = std::numeric_limits<int>::min();
        baseline.match = PWR::MatchExpression{}; // catch-all All{}
        baseline.actions = {intGapAction(PWR::ActionType::SetInnerGap, 4)};

        // A non-managed per-monitor gap override RULE for DP-1 (inner = 20). The
        // settings page authors per-monitor gaps as config now, but the rule
        // cascade still resolves a hand-authored gap rule keyed on a v5 id
        // namespaced under the baseline gap id — this pins that cascade behavior.
        PWR::Rule perScreen;
        perScreen.id = QUuid::createUuidV5(ConfigDefaults::baselineGapRuleId(), QByteArrayLiteral("DP-1"));
        perScreen.name = QStringLiteral("Gaps (DP-1)");
        perScreen.enabled = true;
        perScreen.managed = false;
        perScreen.priority = 310; // context band, well above the baseline floor
        perScreen.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        perScreen.actions = {intGapAction(PWR::ActionType::SetInnerGap, 20)};

        QVERIFY(f.store->setAllRules({baseline, perScreen}));

        // DP-1 carries the per-monitor override → inner gap 20 surfaces as a
        // tier-1 context override (it beats the excluded baseline).
        const PhosphorZones::ContextGapOverride dp1 =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(dp1.innerGap.has_value());
        QCOMPARE(*dp1.innerGap, 20);

        // DP-2 has no per-monitor rule. The managed catch-all baseline is
        // EXCLUDED, so there is NO context override — the cascade falls through
        // to the global default tier (the baseline's value, surfaced elsewhere).
        QVERIFY(f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString()).isEmpty());
    }

    // ─── Context overlay-property resolution (OverlayShader / OverlayStyle) ──
    // resolveContextOverlay is a per-slot read across all matching context
    // rules (mirrors resolveContextGaps): independent shader / style rules
    // compose, and the style wire token maps to the OverlayDisplayMode int.

    void testContextOverlay_perSlotComposition()
    {
        const auto overlayRule = [](const QString& name, int priority, const QString& screenId,
                                    const QList<PWR::RuleAction>& actions) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = name;
            r.enabled = true;
            r.priority = priority;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, screenId);
            r.actions = actions;
            return r;
        };
        const auto shaderAction = [](const QString& id) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayShader);
            a.params.insert(QString(PWR::ActionParam::EffectId), id);
            return a;
        };
        const auto styleAction = [](const QString& token) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayStyle);
            a.params.insert(QString(PWR::ActionParam::Value), token);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();
        // One rule sets ONLY the overlay shader; a separate rule sets ONLY the
        // overlay style. Different slots → both compose (no shadowing).
        const PWR::Rule sh = overlayRule(QStringLiteral("sh"), 400, QStringLiteral("DP-1"),
                                         {shaderAction(QStringLiteral("plasma-glow"))});
        const PWR::Rule st = overlayRule(QStringLiteral("st"), 300, QStringLiteral("DP-1"),
                                         {styleAction(QString(PWR::OverlayStyleToken::Preview))});
        QVERIFY(f.store->setAllRules({sh, st}));

        const PhosphorZones::ContextOverlayOverride resolved =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.shaderId.has_value());
        QCOMPARE(*resolved.shaderId, QStringLiteral("plasma-glow"));
        QVERIFY(resolved.style.has_value());
        QCOMPARE(*resolved.style, 1); // "preview" → OverlayDisplayMode::LayoutPreview

        // A context the rules do not pin → no override (falls through to layout).
        QVERIFY(f.registry->resolveContextOverlay(QStringLiteral("DP-2"), 0, QString()).isEmpty());

        // The "rectangles" token maps to OverlayDisplayMode::ZoneRectangles (0).
        const PWR::Rule rect = overlayRule(QStringLiteral("rect"), 500, QStringLiteral("HDMI-1"),
                                           {styleAction(QString(PWR::OverlayStyleToken::Rectangles))});
        QVERIFY(f.store->setAllRules({rect}));
        const PhosphorZones::ContextOverlayOverride r2 =
            f.registry->resolveContextOverlay(QStringLiteral("HDMI-1"), 0, QString());
        QVERIFY(r2.style.has_value());
        QCOMPARE(*r2.style, 0);
        QVERIFY(!r2.shaderId.has_value());

        // shaderParams round-trip: a shader override carrying a params object
        // resolves it into ContextOverlayOverride::shaderParams (the headline
        // shader-uniform-override feature). The nested object is stored as JSON
        // and decoded via toObject().toVariantMap().
        const auto shaderWithParams = [](const QString& id, const QJsonObject& params) {
            PWR::RuleAction a;
            a.type = QString(PWR::ActionType::OverrideOverlayShader);
            a.params.insert(QString(PWR::ActionParam::EffectId), id);
            a.params.insert(QString(PWR::ActionParam::Params), params);
            return a;
        };
        QJsonObject uniforms;
        uniforms.insert(QStringLiteral("intensity"), 0.5);
        const PWR::Rule shp = overlayRule(QStringLiteral("shp"), 600, QStringLiteral("DVI-1"),
                                          {shaderWithParams(QStringLiteral("plasma-glow"), uniforms)});
        QVERIFY(f.store->setAllRules({shp}));
        const PhosphorZones::ContextOverlayOverride r3 =
            f.registry->resolveContextOverlay(QStringLiteral("DVI-1"), 0, QString());
        QVERIFY(r3.shaderId.has_value());
        QCOMPARE(*r3.shaderId, QStringLiteral("plasma-glow"));
        QCOMPARE(r3.shaderParams.value(QStringLiteral("intensity")).toDouble(), 0.5);
    }

    // ─── Context overlay-APPEARANCE resolution (SetOverlay* colours / opacities
    //     / border dimensions / zone-number visibility) ────────────────────────
    // The appearance actions layer over the global Snapping.Zones.* config: each
    // fills its own optional on ContextOverlayOverride, and an unmatched context
    // leaves them all unset so the consumer falls through to config.
    void testContextOverlay_appearanceOverrides()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        PWR::Rule r;
        r.id = QUuid::createUuid();
        r.name = QStringLiteral("overlay appearance");
        r.enabled = true;
        r.priority = 400;
        r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        r.actions = {
            valueAction(PWR::ActionType::SetOverlayHighlightColor, QStringLiteral("#FF112233")),
            valueAction(PWR::ActionType::SetOverlayInactiveColor, QStringLiteral("#80445566")),
            valueAction(PWR::ActionType::SetOverlayBorderColor, QStringLiteral("#FFEEDDCC")),
            valueAction(PWR::ActionType::SetOverlayActiveOpacity, 0.5),
            valueAction(PWR::ActionType::SetOverlayInactiveOpacity, 0.25),
            valueAction(PWR::ActionType::SetOverlayBorderWidth, 3),
            valueAction(PWR::ActionType::SetOverlayBorderRadius, 12),
            valueAction(PWR::ActionType::SetOverlayShowZoneNumbers, false),
        };

        RegistryFixture f = makeRegistryFixture();
        QVERIFY(f.store->setAllRules({r}));

        const PhosphorZones::ContextOverlayOverride resolved =
            f.registry->resolveContextOverlay(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.highlightColor.has_value());
        QCOMPARE(*resolved.highlightColor, QColor(QStringLiteral("#FF112233")));
        QVERIFY(resolved.inactiveColor.has_value());
        QCOMPARE(*resolved.inactiveColor, QColor(QStringLiteral("#80445566")));
        QVERIFY(resolved.borderColor.has_value());
        QCOMPARE(*resolved.borderColor, QColor(QStringLiteral("#FFEEDDCC")));
        QVERIFY(resolved.activeOpacity.has_value());
        QCOMPARE(*resolved.activeOpacity, 0.5);
        QVERIFY(resolved.inactiveOpacity.has_value());
        QCOMPARE(*resolved.inactiveOpacity, 0.25);
        QVERIFY(resolved.borderWidth.has_value());
        QCOMPARE(*resolved.borderWidth, 3);
        QVERIFY(resolved.borderRadius.has_value());
        QCOMPARE(*resolved.borderRadius, 12);
        QVERIFY(resolved.showZoneNumbers.has_value());
        QCOMPARE(*resolved.showZoneNumbers, false);

        // An unpinned context leaves every appearance field unset (config wins).
        QVERIFY(f.registry->resolveContextOverlay(QStringLiteral("DP-2"), 0, QString()).isEmpty());
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
            PWR::Rule r;
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
        const PWR::Rule lockMonitor =
            lockRule(QStringLiteral("lock DP-1"), PWR::Field::ScreenId, QStringLiteral("DP-1"), true);
        const PWR::Rule lockActivity =
            lockRule(QStringLiteral("lock work"), PWR::Field::Activity, QStringLiteral("work-uuid"), true);
        const PWR::Rule unlockMonitor =
            lockRule(QStringLiteral("unlock DP-3"), PWR::Field::ScreenId, QStringLiteral("DP-3"), false);
        // A desktop-scoped lock: fires on virtual desktop 2 regardless of
        // screen/activity, proving the desktop axis of the context match.
        const PWR::Rule lockDesktop = lockRule(QStringLiteral("lock desktop 2"), PWR::Field::VirtualDesktop, 2, true);
        // A mixed (context + window-property) lock rule: All{ScreenId == DP-4,
        // AppId == firefox} carrying a LockContext action at a far-above band.
        // Against the windowless context query the AppId leaf evaluates false,
        // so the All{} fails and DP-4 must NOT lock — symmetric to the
        // assignment-path mixed-rule inertness proof (testMixedRule* above).
        PWR::RuleAction mixedLockAction;
        mixedLockAction.type = QString(PWR::ActionType::LockContext);
        mixedLockAction.params.insert(QString(PWR::ActionParam::Value), true);
        PWR::Rule mixedLock;
        mixedLock.id = QUuid::createUuid();
        mixedLock.name = QStringLiteral("mixed lock DP-4");
        mixedLock.enabled = true;
        mixedLock.priority = 999;
        mixedLock.match = PWR::MatchExpression::makeAll(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-4")),
             PWR::MatchExpression::makeLeaf(PWR::Field::AppId, PWR::Operator::Equals, QStringLiteral("firefox"))});
        mixedLock.actions = {mixedLockAction};
        QVERIFY(f.store->setAllRules({lockMonitor, lockActivity, unlockMonitor, lockDesktop, mixedLock}));

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
        // The mixed rule's AppId leaf can't match a windowless query → DP-4 not
        // locked, even though its band (999) would dominate if it leaked.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-4"), 0, QString()));
        // A context no rule pins → not locked.
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("HDMI-1"), 0, QString()));

        // Disabling the lock rule drops the lock (revision-invalidated cache):
        // DP-1 was primed locked above, so a stale cache would keep returning
        // true here — the post-mutation false proves the revision bump evicts.
        PWR::Rule disabled = lockMonitor;
        disabled.enabled = false;
        QVERIFY(f.store->setAllRules({disabled, lockActivity, unlockMonitor, lockDesktop, mixedLock}));
        QVERIFY(!f.registry->resolveContextLocked(QStringLiteral("DP-1"), 0, QString()));
        // The eviction is a whole-cache drop, not a zeroing: a lock left intact
        // (lockDesktop, also primed above) must still resolve true after the
        // revision bump rebuilds the cache.
        QVERIFY(f.registry->resolveContextLocked(QStringLiteral("HDMI-2"), 2, QString()));
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
            PWR::Rule r;
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
        const PWR::Rule dp9High = lockRuleAt(QStringLiteral("dp9 unlock"), QStringLiteral("DP-9"), false, 500);
        const PWR::Rule dp9Low = lockRuleAt(QStringLiteral("dp9 lock"), QStringLiteral("DP-9"), true, 400);
        // DP-10: the inverse — higher-priority rule says locked → wins.
        const PWR::Rule dp10High = lockRuleAt(QStringLiteral("dp10 lock"), QStringLiteral("DP-10"), true, 500);
        const PWR::Rule dp10Low = lockRuleAt(QStringLiteral("dp10 unlock"), QStringLiteral("DP-10"), false, 400);
        // DP-11: equal priority, lock=true first — first-listed rule wins.
        const PWR::Rule dp11First = lockRuleAt(QStringLiteral("dp11 a"), QStringLiteral("DP-11"), true, 400);
        const PWR::Rule dp11Second = lockRuleAt(QStringLiteral("dp11 b"), QStringLiteral("DP-11"), false, 400);
        // DP-12: the inverse tie-break — lock=false first at the same priority.
        // Run both directions so the tie-break is proven to be list-order, not
        // value-bias: a "true always wins on a tie" bug would pass DP-11 alone.
        const PWR::Rule dp12First = lockRuleAt(QStringLiteral("dp12 a"), QStringLiteral("DP-12"), false, 400);
        const PWR::Rule dp12Second = lockRuleAt(QStringLiteral("dp12 b"), QStringLiteral("DP-12"), true, 400);
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
        PWR::Rule lock;
        lock.id = QUuid::createUuid();
        lock.name = QStringLiteral("lock DP-7");
        lock.enabled = true;
        lock.priority = 400;
        lock.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-7"));
        lock.actions = {lockAction};

        const PWR::Rule assign =
            CRB::makeAssignmentRule(QStringLiteral("layout DP-7"), QStringLiteral("DP-7"), 0, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("{ctx-layout}"), QString(), 301);
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

    // ─── Per-mode gap rule resolves only for its mode ─────────────────────
    // A `Mode Equals "tiling"` gap rule is context-only (Mode is a context
    // field), so it participates in the gap cascade. resolveContextGaps must
    // pick up its inner gap when the asking engine is tiling, and ignore it
    // when the asking engine is snapping — the whole point of routing per-mode
    // gaps through the context `Mode` field instead of window-property IsTiled.

    void testPerModeGapRuleResolvesForMatchingModeOnly()
    {
        RegistryFixture f = makeRegistryFixture();

        PWR::RuleAction gapAction;
        gapAction.type = QString(PWR::ActionType::SetInnerGap);
        gapAction.params.insert(QString(PWR::ActionParam::Value), 14);
        PWR::Rule tilingGap;
        tilingGap.id = QUuid::createUuid();
        tilingGap.name = QStringLiteral("Tiling inner gap");
        tilingGap.enabled = true;
        tilingGap.priority = 500;
        tilingGap.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"));
        tilingGap.actions = {gapAction};
        QVERIFY(tilingGap.match.isContextOnly());
        QVERIFY(f.store->setAllRules({tilingGap}));

        // Tiling engine asks → the per-mode gap applies.
        const PhosphorZones::ContextGapOverride tiled =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("tiling"));
        QVERIFY(tiled.innerGap.has_value());
        QCOMPARE(*tiled.innerGap, 14);

        // Snapping engine asks → the Mode leaf is a non-match, so no override.
        const PhosphorZones::ContextGapOverride snapped =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString(), QStringLiteral("snapping"));
        QVERIFY(!snapped.innerGap.has_value());

        // No mode supplied (mode-agnostic caller) → also a non-match.
        const PhosphorZones::ContextGapOverride none =
            f.registry->resolveContextGaps(QStringLiteral("DP-9"), 1, QString());
        QVERIFY(!none.innerGap.has_value());
    }

    // ─── Per-monitor gap beats a global per-mode gap (specificity, not priority) ─
    // A per-monitor (ScreenId-pinned) gap override and a global per-mode
    // (Mode-pinned) gap rule can both match the same window/slot. A hand-authored
    // per-mode gap rule can even carry a HIGHER raw priority (500) than a
    // per-screen rule (300). resolveContextGaps must therefore order the slot by
    // MATCH SPECIFICITY (ScreenId-pinned > Mode-pinned), so the per-monitor
    // override wins despite its lower priority, while a slot the per-monitor rule
    // does NOT carry still falls through to the per-mode rule. (Appearance/gaps are
    // config-backed now, so migration creates no gap rules; these are authored
    // directly to pin the cascade contract.)
    void testPerScreenGapBeatsPerModeGap()
    {
        const auto intGapAction = [](QLatin1StringView type, int value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), value);
            return a;
        };

        RegistryFixture f = makeRegistryFixture();

        // Global per-mode gap rule: higher raw priority, carries both inner and
        // outer gap.
        PWR::Rule perMode;
        perMode.id = QUuid::createUuid();
        perMode.name = QStringLiteral("Tiling gaps");
        perMode.enabled = true;
        perMode.priority = 500; // the per-mode rule's priority — deliberately higher
        perMode.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::Mode, PWR::Operator::Equals, QStringLiteral("tiling"));
        perMode.actions = {intGapAction(PWR::ActionType::SetInnerGap, 14),
                           intGapAction(PWR::ActionType::SetOuterGap, 30)};

        // Per-monitor override for DP-1: lower raw priority, carries ONLY inner gap.
        PWR::Rule perScreen;
        perScreen.id = QUuid::createUuid();
        perScreen.name = QStringLiteral("Gaps (DP-1)");
        perScreen.enabled = true;
        perScreen.priority = 300; // the per-monitor rule's priority — deliberately lower
        perScreen.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        perScreen.actions = {intGapAction(PWR::ActionType::SetInnerGap, 20)};

        QVERIFY(f.store->setAllRules({perMode, perScreen}));

        // DP-1 in tiling mode: both rules match the inner-gap slot. The
        // ScreenId-pinned rule is more specific, so its value (20) wins even
        // though the Mode-pinned rule has the higher priority (500 > 300).
        const PhosphorZones::ContextGapOverride dp1 =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString(), QStringLiteral("tiling"));
        QVERIFY(dp1.innerGap.has_value());
        QCOMPARE(*dp1.innerGap, 20);
        // The outer-gap slot is carried only by the per-mode rule, so it still
        // surfaces from there (per-slot composition is preserved).
        QVERIFY(dp1.outerGap.has_value());
        QCOMPARE(*dp1.outerGap, 30);

        // DP-2 in tiling mode: no per-monitor rule, so the per-mode gap applies.
        const PhosphorZones::ContextGapOverride dp2 =
            f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString(), QStringLiteral("tiling"));
        QVERIFY(dp2.innerGap.has_value());
        QCOMPARE(*dp2.innerGap, 14);
    }
};

QTEST_MAIN(TestRuleCascadeFidelity)
#include "test_rule_cascade_fidelity.moc"

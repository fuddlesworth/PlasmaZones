// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_cascade_context.cpp
 * @brief Context-slot cascade proofs for the window-rule model.
 *
 * Split out from test_rule_cascade_fidelity.cpp. Where that suite pins the
 * engine-mode / layout assignment cascade, this one covers the non-assignment
 * context resolvers that share the same priority-wins, per-slot-composition
 * model: gaps (including the per-monitor override), orientation and
 * active-layout stamping (including the scrolling template's prefixed
 * ActiveLayout stamp), autotile tiling params, scrolling context params, and
 * the exactContextEntry pair pinning the settings UI's explicit-versus-resolved
 * discriminator, including its deliberate blindness to a rule's enabled flag.
 *
 * The overlay, lock, per-mode gap routing, negation-polarity and specificity
 * halves live in the sibling test_rule_cascade_overlay_lock.cpp, split off at
 * the overlay banner when this file passed the 1150-line ceiling. Both halves
 * share the harness in RuleCascadeFixture.h.
 */

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTest>
#include <QUuid>
#include <limits>

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/Zone.h>

#include "RuleCascadeFixture.h"

class TestRuleCascadeContext : public QObject, public RuleCascadeFixture
{
    Q_OBJECT

private Q_SLOTS:

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
        // A distinctive inner gap: 0 would be indistinguishable from a
        // default-constructed int if the slot were ever filled by accident.
        const PWR::Rule pad = gapRule(QStringLiteral("pad"), 400, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetInnerGap, 7)});
        const PWR::Rule gap = gapRule(QStringLiteral("gap"), 300, QStringLiteral("DP-1"),
                                      {intGapAction(PWR::ActionType::SetOuterGap, 12)});
        QVERIFY(f.store->setAllRules({pad, gap}));

        const PhosphorZones::ContextGapOverride resolved =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(resolved.innerGap.has_value());
        QCOMPARE(*resolved.innerGap, 7);
        QVERIFY(resolved.outerGap.has_value()); // separate slot — composes, not shadowed
        QCOMPARE(*resolved.outerGap, 12);
        QVERIFY(!resolved.usePerSideOuterGap.has_value());

        // An explicit ZERO is a real override, not an absent one: the optional
        // has to carry it through, or "no gaps on this screen" reads as unset
        // and the global default comes back instead.
        const PWR::Rule zeroPad = gapRule(QStringLiteral("zero-pad"), 400, QStringLiteral("DP-1"),
                                          {intGapAction(PWR::ActionType::SetInnerGap, 0)});
        QVERIFY(f.store->setAllRules({zeroPad, gap}));
        const PhosphorZones::ContextGapOverride zeroed =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(zeroed.innerGap.has_value());
        QCOMPARE(*zeroed.innerGap, 0);

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
    // assignmentIdForScreen must never re-enter the gap resolver). Unbounded
    // recursion here would hang or blow the stack, so the test completing is
    // evidence against that specific failure. It says nothing about a bounded
    // re-entry, which is not observable from here.
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

    // ─── The scrolling TEMPLATE rides the ActiveLayout stamp, prefixed ────────
    // A Scrolling context stamps "scrolling:<templateUuid>" (parity with
    // autotile's "autotile:<algo>"), so a rule can target one specific
    // template; the bare sentinel now means "scrolling with no template".
    void testContextActiveLayout_scrollingTemplateStamp()
    {
        // The store is declared BEFORE the fixture so it outlives the registry
        // that borrows it: locals are destroyed in reverse, and a registry torn
        // down while still holding a pointer into a dead store is a dangling
        // read in whatever the destructor touches.
        PhosphorZones::ScrollingTemplateStore store;
        RegistryFixture f = makeRegistryFixture();
        f.registry->setScrollingTemplateStore(&store);
        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Template");
        templ.presetColumnWidths = {0.5};
        const QString templId = store.saveTemplate(templ).toString();

        const auto gapRuleForActiveLayout = [](const QString& id) {
            PWR::RuleAction gapAction;
            gapAction.type = QString(PWR::ActionType::SetInnerGap);
            gapAction.params.insert(QString(PWR::ActionParam::Value), 21);
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("template gap");
            r.enabled = true;
            r.priority = 400;
            r.match = PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, id);
            r.actions = {gapAction};
            return r;
        };
        const auto innerGapOn = [&f](const QString& screenId) {
            return f.registry->resolveContextGaps(screenId, 0, QString());
        };

        // Scrolling context, no template yet: the BARE sentinel matches.
        // The gap rules are added/removed individually — the ASSIGNMENT lives
        // in the same rule store, so a setAllRules would wipe it.
        f.registry->assignLayoutById(QStringLiteral("DP-1"), 0, QString(),
                                     QString(PhosphorLayout::LayoutId::ScrollingId));
        const PWR::Rule bareRule = gapRuleForActiveLayout(QString(PhosphorLayout::LayoutId::ScrollingId));
        QVERIFY(f.store->addRule(bareRule));
        const PhosphorZones::ContextGapOverride onBare = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onBare.innerGap.has_value());
        QCOMPARE(*onBare.innerGap, 21);

        // Assign the template: the stamp becomes the PREFIXED form — the bare
        // rule stops matching, the prefixed rule fires. Every write here goes
        // through the rule store, so the gap cache's revision compare
        // refreshes it on its own.
        f.registry->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), templId);
        QVERIFY(!innerGapOn(QStringLiteral("DP-1")).innerGap.has_value());
        QVERIFY(f.store->removeRule(bareRule.id));
        const PWR::Rule prefixedRule = gapRuleForActiveLayout(PhosphorLayout::LayoutId::makeScrollingId(templId));
        QVERIFY(f.store->addRule(prefixedRule));
        const PhosphorZones::ContextGapOverride onTemplate = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onTemplate.innerGap.has_value());
        QCOMPARE(*onTemplate.innerGap, 21);

        // Clearing the template restores the bare stamp: the prefixed rule
        // goes inert again.
        f.registry->assignScrollingTemplate(QStringLiteral("DP-1"), 0, QString(), QString());
        QVERIFY(!innerGapOn(QStringLiteral("DP-1")).innerGap.has_value());

        // Inertness alone would also hold if the stamp had gone empty rather
        // than back to the bare sentinel, so re-add the bare rule and require
        // it to fire again. This is what pins the revert to the SENTINEL.
        QVERIFY(f.store->addRule(bareRule));
        const PhosphorZones::ContextGapOverride onCleared = innerGapOn(QStringLiteral("DP-1"));
        QVERIFY(onCleared.innerGap.has_value());
        QCOMPARE(*onCleared.innerGap, 21);
    }

    // ─── An ActiveLayout-referencing rule must NOT drive the assignment ───────
    // The assignment resolver leaves Field::ActiveLayout unstamped (it IS the
    // resolver's output — stamping would recurse). An assignment rule whose match
    // references ActiveLayout is therefore structurally excluded from the
    // assignment path. This matters most for a NEGATED predicate: a positive
    // `ActiveLayout Equals X` leaf never matches the unstamped placeholder, but a
    // `None{ActiveLayout Equals X}` ("active layout is NOT X") would spuriously
    // match the empty placeholder and force a wrong assignment without the guard.
    // The resolve completing is likewise evidence only against UNBOUNDED
    // recursion (which would hang or blow the stack), not against re-entry.
    void testActiveLayoutRuleExcludedFromAssignment()
    {
        RegistryFixture f = makeRegistryFixture();
        // Snap default so a leaked autotile assignment is unambiguously visible.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        // Positive control: an assignment rule pinned by ScreenId DOES drive the
        // assignment — proving the harness observes assignment-driving, so the
        // negative assertions below are meaningful (not vacuously always-Snapping).
        PWR::Rule screenPinned;
        screenPinned.id = QUuid::createUuid();
        screenPinned.name = QStringLiteral("screen-pinned-autotile");
        screenPinned.enabled = true;
        screenPinned.priority = 500;
        screenPinned.match =
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1"));
        screenPinned.actions =
            CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"), QString());
        QVERIFY(f.store->setAllRules({screenPinned}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).mode,
                 PhosphorZones::AssignmentEntry::Autotile);

        // Build the same autotile assignment rule but match on ActiveLayout, once
        // positively and once negated. Neither may drive the assignment: the
        // resolver must fall through to the Snapping gated default.
        const QString someLayout = QStringLiteral("{11111111-1111-1111-1111-111111111111}");
        const auto activeLayoutAssignmentRule = [&](const PWR::MatchExpression& match) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("active-layout assignment");
            r.enabled = true;
            r.priority = 500;
            r.match = match;
            r.actions =
                CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"), QString());
            return r;
        };

        // Positive ActiveLayout leaf — excluded (also never matched the placeholder).
        QVERIFY(f.store->setAllRules({activeLayoutAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout))}));
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).mode,
                 PhosphorZones::AssignmentEntry::Snapping);

        // Negated ActiveLayout predicate — the regression case. Without the
        // referencesAnyField guard this None{} matches the empty placeholder and
        // forces Autotile; with it, the rule is excluded and Snapping stands.
        QVERIFY(f.store->setAllRules({activeLayoutAssignmentRule(PWR::MatchExpression::makeNone(
            {PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout)}))}));
        const PhosphorZones::AssignmentEntry negatedEntry =
            f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(negatedEntry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(negatedEntry.snappingLayout, QStringLiteral("{provider-snap-default}"));
    }

    // ─── ActiveLayout exclusion on the SECOND no-stamp resolver ──────────────
    // resolveContextDefaultAssignment is the other assignment-cascade resolver
    // that leaves ActiveLayout unstamped, so it applies the same referencesAnyField
    // exclusion. A DefaultLayoutAssignment rule matched by None{ActiveLayout Equals
    // X} would, without the guard, match the empty placeholder and force the default
    // through (defeating the global suppress); with the guard it is excluded.
    void testActiveLayoutRuleExcludedFromDefaultAssignment()
    {
        RegistryFixture f = makeRegistryFixture();
        // Global default: suppress the synthesized default assignment everywhere.
        f.registry->setDefaultAssignmentSuppressedProvider([]() {
            return true;
        });

        const auto defaultAssignmentRule = [](const PWR::MatchExpression& match, bool allow) {
            PWR::Rule r;
            r.id = QUuid::createUuid();
            r.name = QStringLiteral("default-assignment");
            r.enabled = true;
            r.priority = 500;
            r.match = match;
            PWR::RuleAction action;
            action.type = QString(PWR::ActionType::DefaultLayoutAssignment);
            action.params.insert(QString(PWR::ActionParam::Value), allow);
            r.actions.append(action);
            return r;
        };

        // Positive control: a ScreenId-pinned allow rule DOES override the global
        // suppress (proves the harness observes default-assignment overrides, so the
        // negatives below are meaningful rather than vacuously always-suppressed).
        QVERIFY(f.store->setAllRules({defaultAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ScreenId, PWR::Operator::Equals, QStringLiteral("DP-1")),
            true)}));
        QVERIFY(!f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));

        const QString someLayout = QStringLiteral("{11111111-1111-1111-1111-111111111111}");

        // Negated ActiveLayout predicate — the regression case. Without the guard the
        // None{} matches the empty placeholder and forces the default through
        // (isSuppressed → false); with it, the rule is excluded and the global
        // suppress stands (isSuppressed → true).
        QVERIFY(f.store->setAllRules(
            {defaultAssignmentRule(PWR::MatchExpression::makeNone({PWR::MatchExpression::makeLeaf(
                                       PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout)}),
                                   true)}));
        QVERIFY(f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));

        // Positive ActiveLayout leaf — also excluded (and never matched the placeholder).
        QVERIFY(f.store->setAllRules({defaultAssignmentRule(
            PWR::MatchExpression::makeLeaf(PWR::Field::ActiveLayout, PWR::Operator::Equals, someLayout), true)}));
        QVERIFY(f.registry->isDefaultAssignmentSuppressedForContext(QStringLiteral("DP-1"), 0, QString()));
    }

    // ─── exactContextEntry discriminates EXPLICIT from RESOLVED ──────────────
    // exactContextEntry is the settings UI's explicit-vs-resolved discriminator:
    // it reports only what THIS exact (screen, desktop, activity) tuple has a
    // rule for, and never what the tuple merely inherits. The Monitors page
    // relies on the distinction — re-pinning a cascade default as if it were
    // explicit would freeze an inherited value into a rule the user never
    // authored. The cascade resolver (assignmentEntryForScreen) is the foil in
    // each case below: it answers, and exactContextEntry must not.
    void testExactContextEntry_explicitOnly_notCascadeOrDefault()
    {
        RegistryFixture f = makeRegistryFixture();
        // A global snap default, so every unpinned context still RESOLVES to
        // something — otherwise the negative assertions would pass vacuously.
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        // One explicit context rule on the MONITOR axis: (DP-1, no desktop, no
        // activity), so its match is a bare ScreenId leaf that every desktop on
        // DP-1 inherits.
        const PWR::Rule assign = CRB::makeAssignmentRule(
            QStringLiteral("layout DP-1"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"),
            QStringLiteral("{explicit-layout}"), QString(), 301, QString());
        QVERIFY(f.store->setAllRules({assign}));

        // (1) The exact tuple → the stored entry comes back.
        const PhosphorZones::AssignmentEntry exact =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(exact.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(exact.snappingLayout, QStringLiteral("{explicit-layout}"));
        QVERIFY(f.registry->hasExplicitAssignment(QStringLiteral("DP-1"), 0, QString()));

        // (2) A tuple that only INHERITS the value through the cascade. Desktop
        // 5 on the same screen resolves to the monitor rule's layout, but
        // nothing is stored AT (DP-1, 5, "") — the canonical desktop-axis shape
        // that tuple names is a different match than the bare ScreenId leaf —
        // so exactContextEntry must report an empty entry.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 5, QString()).snappingLayout,
                 QStringLiteral("{explicit-layout}"));
        const PhosphorZones::AssignmentEntry inherited =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 5, QString());
        QVERIFY(!inherited.isValid());
        QVERIFY(inherited.snappingLayout.isEmpty());
        QVERIFY(inherited.tilingAlgorithm.isEmpty());
        QVERIFY(!f.registry->hasExplicitAssignment(QStringLiteral("DP-1"), 5, QString()));

        // (3) A tuple that resolves only through the global DEFAULT tier — a
        // different screen entirely. The resolver answers with the provider's
        // id; exactContextEntry stays empty.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-2"), 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
        const PhosphorZones::AssignmentEntry defaulted =
            f.registry->exactContextEntry(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(!defaulted.isValid());
        QVERIFY(defaulted.snappingLayout.isEmpty());
        QVERIFY(!f.registry->hasExplicitAssignment(QStringLiteral("DP-2"), 0, QString()));
    }

    // exactContextEntry is deliberately BLIND to the rule's enabled flag: it
    // reports stored intent, not the effective cascade result, because the
    // settings UI must keep rendering a pin the user switched off. The cascade
    // resolver is the opposite — the evaluator skips disabled rules — so the
    // two must DISAGREE for a disabled pin. That disagreement is the contract.
    void testExactContextEntry_disabledRuleStillReportsStoredEntry()
    {
        RegistryFixture f = makeRegistryFixture();
        f.registry->setDefaultLayoutIdProvider([]() {
            return QStringLiteral("{provider-snap-default}");
        });

        PWR::Rule assign = CRB::makeAssignmentRule(QStringLiteral("layout DP-1"), QStringLiteral("DP-1"), 0, QString(),
                                                   QStringLiteral("snapping"), QStringLiteral("{explicit-layout}"),
                                                   QString(), 301, QString());
        assign.enabled = false;
        QVERIFY(f.store->setAllRules({assign}));

        // Stored intent survives the disable.
        const PhosphorZones::AssignmentEntry exact =
            f.registry->exactContextEntry(QStringLiteral("DP-1"), 0, QString());
        QCOMPARE(exact.snappingLayout, QStringLiteral("{explicit-layout}"));

        // The cascade does not: the disabled rule is skipped and the global
        // default tier answers instead.
        QCOMPARE(f.registry->assignmentEntryForScreen(QStringLiteral("DP-1"), 0, QString()).snappingLayout,
                 QStringLiteral("{provider-snap-default}"));
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

    // ─── Context scrolling-parameter resolution (width / centering / display) ──
    // resolveContextScrollingParams is a per-slot read like its tiling sibling:
    // independent SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
    // SetScrollDefaultColumnDisplay rules compose, and an unpinned screen resolves
    // to an all-unset (empty) params struct.
    void testContextScrollingParams_perSlotComposition()
    {
        const auto valueAction = [](QLatin1StringView type, const QVariant& value) {
            PWR::RuleAction a;
            a.type = QString(type);
            a.params.insert(QString(PWR::ActionParam::Value), QJsonValue::fromVariant(value));
            return a;
        };
        const auto scrollRule = [&](const QString& name, int priority, const QString& screenId,
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
        const PWR::Rule cw = scrollRule(QStringLiteral("cw"), 400, QStringLiteral("DP-1"),
                                        {valueAction(PWR::ActionType::SetScrollDefaultColumnWidth, 0.75)});
        // Centering carries a wire token → the centering int.
        const PWR::Rule cf = scrollRule(
            QStringLiteral("cf"), 300, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetCenterFocusedColumn, QString(PWR::CenterFocusedColumnToken::OnOverflow))});
        // Column display carries a wire token → the display int.
        const PWR::Rule cd = scrollRule(
            QStringLiteral("cd"), 200, QStringLiteral("DP-1"),
            {valueAction(PWR::ActionType::SetScrollDefaultColumnDisplay, QString(PWR::ColumnDisplayToken::Tabbed))});
        // The strip axis carries a wire token → the Scrolling.StripAxis
        // config int (the INTENT space, not the engine's ScrollAxis).
        const PWR::Rule ax =
            scrollRule(QStringLiteral("ax"), 100, QStringLiteral("DP-1"),
                       {valueAction(PWR::ActionType::SetScrollStripAxis, QString(PWR::StripAxisToken::Vertical))});
        QVERIFY(f.store->setAllRules({cw, cf, cd, ax}));

        const PhosphorZones::ContextScrollingParams p =
            f.registry->resolveContextScrollingParams(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(p.defaultColumnWidth.has_value());
        QCOMPARE(*p.defaultColumnWidth, 0.75);
        QVERIFY(p.centerFocusedColumn.has_value());
        QCOMPARE(*p.centerFocusedColumn, 2); // "onOverflow" → 2
        QVERIFY(p.defaultColumnDisplay.has_value());
        QCOMPARE(*p.defaultColumnDisplay, 1); // "tabbed" → 1
        QVERIFY(p.stripAxis.has_value());
        QCOMPARE(*p.stripAxis, 2); // "vertical" → 2

        // A screen the rules do not pin → all-unset (the engine then keeps its
        // config-derived parameters for that screen).
        QVERIFY(f.registry->resolveContextScrollingParams(QStringLiteral("DP-2"), 0, QString()).isEmpty());
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

        // The exclusion is keyed on MANAGED-and-catch-all, not on catch-all
        // alone: a user's own catch-all gap rule is an ordinary context override
        // and must surface on every screen, DP-2 included. Without this arm a
        // guard that dropped all catch-alls would look correct.
        PWR::Rule userCatchAll;
        userCatchAll.id = QUuid::createUuid();
        userCatchAll.name = QStringLiteral("My gaps everywhere");
        userCatchAll.enabled = true;
        userCatchAll.managed = false;
        // Below the per-monitor rule's 310, so the DP-1 arm below is about the
        // per-monitor override winning and not about specificity ordering
        // (which testPerScreenGapBeatsPerModeGap covers on its own).
        userCatchAll.priority = 300;
        userCatchAll.match = PWR::MatchExpression{}; // catch-all All{}
        userCatchAll.actions = {intGapAction(PWR::ActionType::SetInnerGap, 9)};
        QVERIFY(f.store->setAllRules({baseline, perScreen, userCatchAll}));

        const PhosphorZones::ContextGapOverride dp2User =
            f.registry->resolveContextGaps(QStringLiteral("DP-2"), 0, QString());
        QVERIFY(dp2User.innerGap.has_value());
        QCOMPARE(*dp2User.innerGap, 9);
        // DP-1 still prefers its per-monitor rule over the user catch-all.
        const PhosphorZones::ContextGapOverride dp1User =
            f.registry->resolveContextGaps(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(dp1User.innerGap.has_value());
        QCOMPARE(*dp1User.innerGap, 20);
    }
};

QTEST_MAIN(TestRuleCascadeContext)
#include "test_rule_cascade_context.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_rule_cascade_context.cpp
 * @brief Context-slot cascade proofs for the window-rule model.
 *
 * Split out from test_rule_cascade_fidelity.cpp. Where that suite pins the
 * engine-mode / layout assignment cascade, this one covers the non-assignment
 * context resolvers that share the same priority-wins, per-slot-composition
 * model: gaps, orientation / active-layout stamping, autotile tiling params,
 * overlay shader / style / appearance overrides, context locks, and the
 * per-mode gap routing through the context `Mode` field. The shared harness
 * lives in RuleCascadeFixture.h.
 */

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTest>
#include <QUuid>
#include <limits>

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

    // ─── An ActiveLayout-referencing rule must NOT drive the assignment ───────
    // The assignment resolver leaves Field::ActiveLayout unstamped (it IS the
    // resolver's output — stamping would recurse). An assignment rule whose match
    // references ActiveLayout is therefore structurally excluded from the
    // assignment path. This matters most for a NEGATED predicate: a positive
    // `ActiveLayout Equals X` leaf never matches the unstamped placeholder, but a
    // `None{ActiveLayout Equals X}` ("active layout is NOT X") would spuriously
    // match the empty placeholder and force a wrong assignment without the guard.
    // The resolve also completing at all proves the no-recursion contract.
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
            CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"));
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
            r.actions = CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QStringLiteral("dwindle"));
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

QTEST_MAIN(TestRuleCascadeContext)
#include "test_rule_cascade_context.moc"

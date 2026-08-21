// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <QJsonArray>
#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;

class TestRule : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testValidRule()
    {
        const Rule r =
            makeRule(QStringLiteral("Firefox autotile"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        QVERIFY(r.isValid());
    }

    void testInvalidRule_nullId()
    {
        Rule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        r.id = QUuid();
        QVERIFY(!r.isValid());
    }

    void testInvalidRule_badMatch()
    {
        Rule r =
            makeRule(QStringLiteral("x"), 0,
                     MatchExpression::makeLeaf(Field::Pid, Operator::Contains, QStringLiteral("12")), {floatAction()});
        QVERIFY(!r.isValid());
    }

    void testInvalidRule_zeroActions()
    {
        // A rule with no actions fills no slot, so isValid() rejects it and
        // RuleSet refuses to store it. Without the actions.isEmpty() guard the
        // rule would live in memory until the next save/load round-trip
        // silently dropped it, so assert both halves.
        Rule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        r.actions.clear();
        QVERIFY(!r.isValid());

        RuleSet set;
        QVERIFY(!set.addRule(r));
        QVERIFY(set.rules().isEmpty());

        // Discriminator: the same rule WITH an action is accepted, so the
        // refusal above is the empty action list and not the id or the match.
        r.actions.append(floatAction());
        QVERIFY(r.isValid());
        QVERIFY(set.addRule(r));
        QCOMPARE(set.rules().size(), 1);
    }

    void testHasTerminalAction()
    {
        const Rule excludeRule = makeRule(QStringLiteral("excl"), 0, MatchExpression{}, {excludeAction()});
        QVERIFY(excludeRule.hasTerminalAction());

        const Rule plainRule = makeRule(QStringLiteral("plain"), 0, MatchExpression{}, {floatAction()});
        QVERIFY(!plainRule.hasTerminalAction());
    }

    void testJson_roundTrip()
    {
        const Rule r =
            makeRule(QStringLiteral("Keep VS Code dialogs floating"), 720,
                     MatchExpression::makeAll({
                         MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("code")),
                         MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, 2),
                     }),
                     {floatAction(), setOpacity(0.95), engineMode(QStringLiteral("snapping"))});

        const auto reloaded = Rule::fromJson(r.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, r);
    }

    void testJson_idHasBraces()
    {
        const Rule r = makeRule(QStringLiteral("x"), 0, MatchExpression{}, {floatAction()});
        const QString idStr = r.toJson().value(QStringLiteral("id")).toString();
        QVERIFY(idStr.startsWith(QLatin1Char('{')));
        QVERIFY(idStr.endsWith(QLatin1Char('}')));
    }

    void testJson_enabledDefaultsTrueWhenAbsent()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("name"), QStringLiteral("x"));
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QJsonArray actions;
        actions.append(floatAction().toJson());
        o.insert(QStringLiteral("actions"), actions);
        // No `enabled` key.
        const auto reloaded = Rule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QVERIFY(reloaded->enabled);
    }

    void testJson_dropsRuleWithInvalidId()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QStringLiteral("not-a-uuid"));
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QVERIFY(!Rule::fromJson(o).has_value());
    }

    void testJson_dropsRuleWithNoValidActions()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        // An action whose type is unregistered — dropped — leaving zero.
        QJsonArray actions;
        actions.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("bogusAction")}});
        o.insert(QStringLiteral("actions"), actions);
        QVERIFY(!Rule::fromJson(o).has_value());
    }

    void testJson_dropsMalformedActionButKeepsRule()
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QUuid::createUuid().toString());
        o.insert(QStringLiteral("match"), QJsonObject{{QStringLiteral("all"), QJsonArray{}}});
        QJsonArray actions;
        actions.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("bogusAction")}});
        actions.append(floatAction().toJson()); // one valid action survives
        o.insert(QStringLiteral("actions"), actions);
        const auto reloaded = Rule::fromJson(o);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->actions.size(), 1);
    }

    // ── validationIssues() ────────────────────────────────────────────────

    void testValidationIssues_catchAllWithContextAction()
    {
        // Catch-all match + context action → no issue. The provider-default
        // rule shape (empty All{}) must stay valid for every action.
        const Rule r = makeRule(QStringLiteral("provider default"), 0, MatchExpression{},
                                {engineMode(QStringLiteral("autotile"))});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_pureContextMatchWithContextAction()
    {
        // A match referencing only context fields is compatible with every
        // context-domain action — this is the bridge-authored assignment-rule
        // shape and must stay quiet.
        const Rule r =
            makeRule(QStringLiteral("display 1"), 310,
                     MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")),
                     {engineMode(QStringLiteral("snapping")), snappingLayout(QStringLiteral("{abc}"))});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_windowMatchWithContextAction()
    {
        // The flagged combination: a context-domain action with a window-class
        // predicate. The match leaf fails on the windowless context query, so
        // the action silently never fires.
        const Rule r =
            makeRule(QStringLiteral("firefox autotile"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile"))});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
        QCOMPARE(issues.first().actionIndex, 0);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetEngineMode));
        QVERIFY(!issues.first().message.isEmpty());
    }

    void testValidationIssues_mixedAllMatchWithContextAction()
    {
        // A flat All{} with both window and context leaves still contains a
        // window leaf, so the rule is non-context-only and the context action
        // is flagged. (The window leaf fails on a context query, dragging the
        // whole All down to false.)
        const Rule r =
            makeRule(QStringLiteral("firefox on display-1"), 200,
                     MatchExpression::makeAll({
                         MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                         MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")),
                     }),
                     {snappingLayout(QStringLiteral("{abc}"))});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
    }

    void testValidationIssues_windowMatchWithWindowAction()
    {
        // Window-domain actions are always compatible — the per-window
        // evaluator carries every field, so the match never silently fails.
        const Rule r =
            makeRule(QStringLiteral("firefox float"), 200,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {floatAction(), setOpacity(0.9)});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_contextMatchWithWindowAction()
    {
        // Context-only match + window action → valid. Means "float every
        // window on screen X" — unusual but a legitimate user intent.
        const Rule r = makeRule(
            QStringLiteral("float everything on display-1"), 310,
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("display-1")), {floatAction()});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_gapActionWithWindowMatch()
    {
        // Gap overrides are context-domain — pairing one with a window-property
        // match silently never fires (the gap is resolved during the windowless
        // context pass). The validator must flag it just like the engine/layout
        // context actions.
        const Rule r =
            makeRule(QStringLiteral("konsole padding"), 200,
                     MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("org.kde.konsole")),
                     {innerGap(0)});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::ContextActionWithWindowMatch);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetInnerGap));
    }

    void testValidationIssues_gapActionWithContextMatch()
    {
        // Gap override + context-only match → valid: "zero padding on activity
        // X" is exactly the intended use of a context gap rule.
        const Rule r = makeRule(
            QStringLiteral("gaming no gaps"), 510,
            MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("gaming-uuid")), {innerGap(0)});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_multipleActionsEachFlaggedIndependently()
    {
        // Two context-domain actions on a window match → two issues, each
        // pointing at its own index, so the UI can pin a marker per action.
        const Rule r =
            makeRule(QStringLiteral("firefox stuff"), 200,
                     MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("autotile")), snappingLayout(QStringLiteral("{abc}")), floatAction()});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 2);
        QCOMPARE(issues.at(0).actionIndex, 0);
        QCOMPARE(issues.at(0).actionType, QString(ActionType::SetEngineMode));
        QCOMPARE(issues.at(1).actionIndex, 1);
        QCOMPARE(issues.at(1).actionType, QString(ActionType::SetSnappingLayout));
        // The window-domain floatAction at index 2 must not be flagged.
    }

    void testValidationIssues_duplicateSameTypeSlotActionsFlagged()
    {
        // Two SetScrollingTemplate actions on one rule fill the same slot with
        // the same type — the growth shape a buggy assignment rebuild
        // accretes. The SECOND occurrence is flagged.
        const Rule r = makeRule(QStringLiteral("doubled template"), 300,
                                MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
                                {engineMode(QStringLiteral("scrolling")), scrollingTemplate(QStringLiteral("{a}")),
                                 scrollingTemplate(QStringLiteral("{b}"))});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::DuplicateSlotActions);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetScrollingTemplate));
        QCOMPARE(issues.first().actionIndex, 2);
    }

    void testValidationIssues_losslessLayoutPairNotFlagged()
    {
        // SetSnappingLayout and SetTilingAlgorithm share the layout slot BY
        // DESIGN (the lossless mode-toggle pair; the active mode picks
        // between them) — different types on one slot must not be flagged.
        // The scrollingTemplate action in the same list is quiet for a
        // DIFFERENT reason: it holds its own slot rather than sharing the
        // layout one, so it never collides here at all. Its duplicate case is
        // testValidationIssues_duplicateSameTypeSlotActionsFlagged above.
        const Rule r = makeRule(QStringLiteral("lossless pair"), 300,
                                MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
                                {engineMode(QStringLiteral("autotile")), snappingLayout(QStringLiteral("{a}")),
                                 tilingAlgorithm(QStringLiteral("dwindle")), scrollingTemplate(QStringLiteral("{b}"))});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_terminalWithSlotActionFlagged()
    {
        // A terminal Exclude co-located with a slot-filling action: the terminal
        // action truncates the evaluator's resolve walk, so the border action may
        // be dropped. Flag the non-terminal action (not the Exclude itself).
        const Rule r =
            makeRule(QStringLiteral("exclude + border"), 500, MatchExpression{}, {excludeAction(), borderWidth(4)});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::TerminalActionWithEffectActions);
        QCOMPARE(issues.first().actionType, QString(ActionType::SetBorderWidth));
        QCOMPARE(issues.first().actionIndex, 1);
    }

    void testValidationIssues_pureExcludeNotFlagged()
    {
        // A rule that is ONLY a terminal Exclude has no co-located slot-filling
        // action, so the terminal co-location check produces nothing.
        const Rule r = makeRule(QStringLiteral("pure exclude"), 500, MatchExpression{}, {excludeAction()});
        QVERIFY(r.validationIssues().isEmpty());
    }

    void testValidationIssues_scopedExcludeWithSlotActionNotFlagged()
    {
        // The scoped exclusions cancel only the siblings resolved by the ONE
        // evaluator that honours them. These pairings cross that boundary, so
        // they are authorable and must stay unflagged: the decoration slice
        // resolves no other slot, and ExcludePlacement is out of scope for the
        // effect evaluator that resolves opacity (a Tag::Effect action).
        const Rule decorations = makeRule(QStringLiteral("undecorate + border"), 500, MatchExpression{},
                                          {excludeDecorationsAction(), borderWidth(4)});
        QVERIFY(decorations.validationIssues().isEmpty());

        const Rule placement = makeRule(QStringLiteral("unplace + opacity"), 500, MatchExpression{},
                                        {excludePlacementAction(), setOpacity(0.8)});
        QVERIFY(placement.validationIssues().isEmpty());
    }

    void testValidationIssues_scopedExcludeCancellingItsOwnSliceFlagged()
    {
        // The other side of the same boundary: pairings the honouring
        // evaluator itself resolves ARE cancelled and must be flagged.
        // ExcludeAnimations terminates the effect evaluator before it can
        // resolve a border override (Tag::Effect)...
        const Rule animations = makeRule(QStringLiteral("unanimate + border"), 500, MatchExpression{},
                                         {excludeAnimationsAction(), borderWidth(2)});
        const auto animIssues = animations.validationIssues();
        QCOMPARE(animIssues.size(), 1);
        QCOMPARE(animIssues.first().code, ValidationIssue::Code::TerminalActionWithEffectActions);
        QCOMPARE(animIssues.first().actionType, QString(ActionType::SetBorderWidth));

        // ...and ExcludePlacement terminates the window-tracking evaluator
        // before it can resolve a float override (window-domain slot action
        // that is not Tag::Effect).
        const Rule placement = makeRule(QStringLiteral("unplace + float"), 500, MatchExpression{},
                                        {excludePlacementAction(), floatAction()});
        const auto placeIssues = placement.validationIssues();
        QCOMPARE(placeIssues.size(), 1);
        QCOMPARE(placeIssues.first().code, ValidationIssue::Code::TerminalActionWithEffectActions);
        QCOMPARE(placeIssues.first().actionType, QString(ActionType::Float));
    }

    void testValidationIssues_effectVerdictActionsNotCancelledByScopedExcludes()
    {
        // The Tag::EffectVerdict actions sit outside BOTH scoped exclusions'
        // slices: the compositor consumes them, so the daemon's placement
        // evaluator never resolves them (ExcludePlacement cannot cancel them),
        // and their effect-side evaluator scopes terminal actions to the
        // blanket Exclude alone (ExcludeAnimations cannot either). That is the
        // entire reason the tag is separate from Tag::Effect, and the
        // classification lives in one place — validationIssues' two tag reads
        // — so pin both directions here.
        //
        // Both tag memberships are pinned too: an EffectVerdict descriptor
        // silently reverted to Tag::Effect would make the ExcludeAnimations
        // pairing below start flagging, but a descriptor that lost BOTH tags
        // would make it start flagging as a placement action instead, and only
        // the membership assertions tell those two regressions apart.
        const ActionRegistry& registry = ActionRegistry::instance();
        for (const QLatin1StringView type : {ActionType::OpenFullscreen, ActionType::ScrollFactor}) {
            QVERIFY2(registry.hasTag(QString(type), Tag::EffectVerdict), type.data());
            QVERIFY2(!registry.hasTag(QString(type), Tag::Effect), type.data());
        }

        RuleAction fullscreen;
        fullscreen.type = QString(ActionType::OpenFullscreen);
        fullscreen.params.insert(QString(ActionParam::Value), true);

        RuleAction scrollFactor;
        scrollFactor.type = QString(ActionType::ScrollFactor);
        scrollFactor.params.insert(QString(ActionParam::Value), 0.5);

        for (const RuleAction& verdict : {fullscreen, scrollFactor}) {
            const Rule animations = makeRule(QStringLiteral("unanimate + verdict"), 500, MatchExpression{},
                                             {excludeAnimationsAction(), verdict});
            QVERIFY2(animations.validationIssues().isEmpty(), qPrintable(verdict.type));

            const Rule placement = makeRule(QStringLiteral("unplace + verdict"), 500, MatchExpression{},
                                            {excludePlacementAction(), verdict});
            QVERIFY2(placement.validationIssues().isEmpty(), qPrintable(verdict.type));

            // The blanket Exclude still cancels them — it is in every
            // evaluator's scope, verdict evaluator included.
            const Rule blanket =
                makeRule(QStringLiteral("exclude + verdict"), 500, MatchExpression{}, {excludeAction(), verdict});
            const auto issues = blanket.validationIssues();
            QCOMPARE(issues.size(), 1);
            QCOMPARE(issues.first().actionType, verdict.type);
        }
    }

    void testValidationIssues_blanketExcludeBesideScopedExcludeFlagged()
    {
        // The blanket Exclude is honoured by every full-store evaluator, so it
        // does cancel its siblings — including a scoped exclusion, which is a
        // co-located action like any other.
        const Rule r = makeRule(QStringLiteral("exclude + undecorate"), 500, MatchExpression{},
                                {excludeAction(), excludeDecorationsAction()});
        const auto issues = r.validationIssues();
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().code, ValidationIssue::Code::TerminalActionWithEffectActions);
        QCOMPARE(issues.first().actionType, QString(ActionType::ExcludeDecorations));
        QCOMPARE(issues.first().actionIndex, 1);
    }
};

QTEST_GUILESS_MAIN(TestRule)
#include "test_rule.moc"

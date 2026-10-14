// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_contextrulebridge.cpp
 * @brief Unit tests for the header-only ContextRuleBridge.
 *
 * Exercises the bridge functions: makeContextMatch (single-leaf collapse vs
 * makeAll vs catch-all), makeAssignmentActions / makeAssignmentRule
 * (assignment-slot losslessness across all four slots + empty-field omission
 * + caller-supplied priority), makeDisableRule, the makeContextMatch →
 * contextDimsOf / disableRuleMode round-trips, and the axis classifiers
 * including the strict Activity / Combined pair the batch APIs key on, and
 * remapContextRules (a desktop removed, inserted or moved, an activity removed).
 */

#include <QTest>

#include <PhosphorRules/ContextRuleBridge.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>

using namespace PhosphorRules;
namespace CRB = PhosphorRules::ContextRuleBridge;

class TestContextRuleBridge : public QObject
{
    Q_OBJECT

private:
    // Convenience: the SetEngineMode action's mode token, or empty if absent.
    static QString modeToken(const QList<RuleAction>& actions)
    {
        for (const RuleAction& a : actions) {
            if (a.type == QString(ActionType::SetEngineMode)) {
                return a.params.value(ActionParam::Mode).toString();
            }
        }
        return QString();
    }

    static int actionCount(const QList<RuleAction>& actions, QLatin1StringView type)
    {
        int n = 0;
        for (const RuleAction& a : actions) {
            if (a.type == QString(type)) {
                ++n;
            }
        }
        return n;
    }

private Q_SLOTS:

    // ─── makeContextMatch ─────────────────────────────────────────────────

    void testMakeContextMatch_nothingPinnedIsCatchAll()
    {
        const MatchExpression m = CRB::makeContextMatch(QString(), 0, QString());
        QVERIFY(m.isCatchAll());
    }

    void testMakeContextMatch_singleDimensionCollapsesToLeaf()
    {
        // One pinned dimension yields a bare leaf, not a one-child All{}.
        const MatchExpression screenOnly = CRB::makeContextMatch(QStringLiteral("DP-1"), 0, QString());
        QVERIFY(screenOnly.isLeaf());
        QCOMPARE(screenOnly.predicate().field, Field::ScreenId);
        QCOMPARE(screenOnly.predicate().value.toString(), QStringLiteral("DP-1"));

        const MatchExpression deskOnly = CRB::makeContextMatch(QString(), 3, QString());
        QVERIFY(deskOnly.isLeaf());
        QCOMPARE(deskOnly.predicate().field, Field::VirtualDesktop);
        QCOMPARE(deskOnly.predicate().value.toInt(), 3);
    }

    void testMakeContextMatch_multipleDimensionsAreFlatAll()
    {
        const MatchExpression m = CRB::makeContextMatch(QStringLiteral("DP-1"), 2, QStringLiteral("act-x"));
        QCOMPARE(m.kind(), MatchExpression::Kind::All);
        QCOMPARE(m.children().size(), 3);
        // Every child is a context-equality leaf.
        for (const MatchExpression& child : m.children()) {
            QVERIFY(child.isLeaf());
            QCOMPARE(child.predicate().op, Operator::Equals);
        }
    }

    // ─── makeAssignmentActions ────────────────────────────────────────────

    void testMakeAssignmentActions_threeActionLossless()
    {
        const QList<RuleAction> actions = CRB::makeAssignmentActions(
            QStringLiteral("snapping"), QStringLiteral("layout-a"), QStringLiteral("algo-b"), QString());
        // Mode + layout + algorithm: the snapping layout and the tiling
        // algorithm both survive a mode flip. The scrolling-template leg of the
        // same losslessness invariant is covered by
        // testMakeAssignmentActions_scrollingTemplateAction below, which is why
        // this case passes an empty template and expects three actions.
        QCOMPARE(actions.size(), 3);
        QCOMPARE(modeToken(actions), QStringLiteral("snapping"));
        QCOMPARE(actionCount(actions, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(actions, ActionType::SetTilingAlgorithm), 1);
    }

    void testMakeAssignmentActions_autotileModeToken()
    {
        const QList<RuleAction> actions =
            CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QString(), QString());
        QCOMPARE(modeToken(actions), QStringLiteral("autotile"));
    }

    void testMakeAssignmentActions_emptyFieldsOmitted()
    {
        // Mode-only entry — all three payload fields empty → a single action.
        const QList<RuleAction> modeOnly =
            CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QString(), QString());
        QCOMPARE(modeOnly.size(), 1);
        QCOMPARE(modeOnly.first().type, QString(ActionType::SetEngineMode));

        // Layout but no algorithm.
        const QList<RuleAction> layoutOnly =
            CRB::makeAssignmentActions(QStringLiteral("snapping"), QStringLiteral("layout-a"), QString(), QString());
        QCOMPARE(layoutOnly.size(), 2);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetTilingAlgorithm), 0);
    }

    void testMakeAssignmentActions_scrollingTemplateAction()
    {
        // The four-field lossless set: mode + snapping layout + algorithm +
        // scrolling template, each in its own action.
        const QList<RuleAction> actions =
            CRB::makeAssignmentActions(QStringLiteral("scrolling"), QStringLiteral("layout-a"),
                                       QStringLiteral("algo-b"), QStringLiteral("template-c"));
        QCOMPARE(actions.size(), 4);
        QCOMPARE(modeToken(actions), QStringLiteral("scrolling"));
        QCOMPARE(actionCount(actions, ActionType::SetScrollingTemplate), 1);
        for (const RuleAction& action : actions) {
            if (action.type == QString(ActionType::SetScrollingTemplate)) {
                QCOMPARE(action.params.value(ActionParam::LayoutId).toString(), QStringLiteral("template-c"));
            }
        }

        // An empty template emits no action — the pre-template
        // three-action shape is unchanged.
        const QList<RuleAction> withoutTemplate = CRB::makeAssignmentActions(
            QStringLiteral("scrolling"), QStringLiteral("layout-a"), QStringLiteral("algo-b"), QString());
        QCOMPARE(withoutTemplate.size(), 3);
        QCOMPARE(actionCount(withoutTemplate, ActionType::SetScrollingTemplate), 0);
    }

    // ─── makeAssignmentRule ───────────────────────────────────────────────

    void testMakeAssignmentRule_carriesScrollingTemplate()
    {
        // The explicit trailing template argument forwards into a
        // SetScrollingTemplate action.
        const Rule rule = CRB::makeAssignmentRule(
            QStringLiteral("Scrolling rule"), QStringLiteral("DP-1"), 2, QStringLiteral("act-x"),
            QStringLiteral("scrolling"), QStringLiteral("layout-a"), QString(), 350, QStringLiteral("template-c"));
        QVERIFY(rule.isValid());
        QCOMPARE(actionCount(rule.actions, ActionType::SetScrollingTemplate), 1);
        for (const RuleAction& action : rule.actions) {
            if (action.type == QString(ActionType::SetScrollingTemplate)) {
                QCOMPARE(action.params.value(ActionParam::LayoutId).toString(), QStringLiteral("template-c"));
            }
        }
    }

    void testMakeAssignmentRule_isValidAndContextOnly()
    {
        const Rule rule = CRB::makeAssignmentRule(QStringLiteral("Exact rule"), QStringLiteral("DP-1"), 2,
                                                  QStringLiteral("act-x"), QStringLiteral("snapping"),
                                                  QStringLiteral("layout-a"), QStringLiteral("algo-b"), 350, QString());
        QVERIFY(rule.isValid());
        QVERIFY(!rule.id.isNull());
        QVERIFY(rule.match.isContextOnly());
        QCOMPARE(rule.name, QStringLiteral("Exact rule"));
        // Priority is the caller's value verbatim — no cascade formula.
        QCOMPARE(rule.priority, 350);
        QCOMPARE(rule.actions.size(), 3);
    }

    void testMakeAssignmentRule_modeOnlyEntry()
    {
        const Rule rule =
            CRB::makeAssignmentRule(QStringLiteral("Display default"), QStringLiteral("HDMI-1"), 0, QString(),
                                    QStringLiteral("autotile"), QString(), QString(), CRB::kContextBandBase, QString());
        QVERIFY(rule.isValid());
        QCOMPARE(rule.priority, CRB::kContextBandBase);
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(modeToken(rule.actions), QStringLiteral("autotile"));
    }

    // ─── makeDisableRule ──────────────────────────────────────────────────

    void testMakeDisableRule_carriesSingleDisableEngineAction()
    {
        const Rule rule = CRB::makeDisableRule(QStringLiteral("Snapping off · DP-1"), QStringLiteral("DP-1"), 0,
                                               QString(), QStringLiteral("snapping"), CRB::kContextBandBase);
        QVERIFY(rule.isValid());
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(rule.actions.first().type, QString(ActionType::DisableEngine));
        QCOMPARE(rule.priority, CRB::kContextBandBase);
    }

    // ─── Round-trip: makeContextMatch → contextDimsOf ─────────────────────

    void testRoundTrip_contextDimsOf_exact()
    {
        const MatchExpression m = CRB::makeContextMatch(QStringLiteral("DP-1"), 4, QStringLiteral("act-x"));
        QString sid;
        int desk = 0;
        QString act;
        CRB::contextDimsOf(m, sid, desk, act);
        QCOMPARE(sid, QStringLiteral("DP-1"));
        QCOMPARE(desk, 4);
        QCOMPARE(act, QStringLiteral("act-x"));
    }

    void testRoundTrip_contextDimsOf_singleLeaf()
    {
        const MatchExpression m = CRB::makeContextMatch(QStringLiteral("HDMI-1"), 0, QString());
        QString sid;
        int desk = 0;
        QString act;
        CRB::contextDimsOf(m, sid, desk, act);
        QCOMPARE(sid, QStringLiteral("HDMI-1"));
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    void testRoundTrip_contextDimsOf_catchAllYieldsDefaults()
    {
        QString sid = QStringLiteral("stale");
        int desk = 99;
        QString act = QStringLiteral("stale");
        CRB::contextDimsOf(MatchExpression(), sid, desk, act);
        QVERIFY(sid.isEmpty());
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    void testContextDimsOf_modeLeafProjectedOut()
    {
        // A flat All{ScreenId, Mode} decodes the screen dimension and projects
        // the Mode leaf OUT: Mode is a context field but NOT one of the three
        // decomposed dimensions (screen/desktop/activity), so decomposition still
        // succeeds with only the screen pinned and the Mode leaf ignored.
        const MatchExpression m = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::Mode, Operator::Equals, QString(ModeToken::Tiling)),
        });
        QString sid;
        int desk = 0;
        QString act;
        const bool ok = CRB::contextDimsOf(m, sid, desk, act);
        QVERIFY(ok);
        QCOMPARE(sid, QStringLiteral("DP-1"));
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    void testTiledWindowCountLeafExcludedFromExactContext()
    {
        // A flat All{ScreenId, TiledWindowCount} is context-only (so it resolves
        // through the evaluator), but it pins MORE than the (screen, desktop,
        // activity) shape makeContextMatch emits. It must NOT be classified as an
        // exact-context assignment: the batch reader/writers and exact-rule
        // upsert would otherwise rebuild the screen base from the decoded screen
        // alone and silently drop the count leaf.
        const MatchExpression m = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::TiledWindowCount, Operator::GreaterThan, 1),
        });
        QVERIFY(m.isContextOnly());
        QCOMPARE(CRB::contextAxisFor(m), CRB::ContextAxis::CatchAll);
        QVERIFY(!CRB::matchIsExactContextBase(m));
        QVERIFY(!CRB::matchIsExactContext(m, QStringLiteral("DP-1"), 0, QString()));
    }

    void testContextDimsOf_nonFlatCompositeYieldsDefaults()
    {
        // An Any{} composite is outside the bridge contract — contextDimsOf
        // must leave all three at their defaults rather than mis-decompose it.
        const MatchExpression any = MatchExpression::makeAny(
            {MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1"))});
        QString sid = QStringLiteral("stale");
        int desk = 7;
        QString act = QStringLiteral("stale");
        const bool ok = CRB::contextDimsOf(any, sid, desk, act);
        QVERIFY(!ok);
        QVERIFY(sid.isEmpty());
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    void testContextDimsOf_refusesDuplicateContextLeaf()
    {
        // A hand-edited rule with two ScreenId Equals leaves under one All{}
        // is undefined input — makeContextMatch can never produce duplicates.
        // contextDimsOf must refuse (return false, leave dims at defaults)
        // rather than silently coerce via last-write-wins.
        const MatchExpression dup = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-2")),
        });
        QString sid = QStringLiteral("stale");
        int desk = 4;
        QString act = QStringLiteral("stale");
        const bool ok = CRB::contextDimsOf(dup, sid, desk, act);
        QVERIFY2(!ok, "duplicate ScreenId leaves must be refused, not coerced");
        QVERIFY(sid.isEmpty());
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    void testContextDimsOf_refusesDuplicateActivityLeaf()
    {
        // Same refusal for a duplicate Activity leaf — covers a second of
        // the three context dimensions (Screen handled above; VirtualDesktop
        // covered by testContextDimsOf_refusesDuplicateVirtualDesktopLeaf
        // below).
        const MatchExpression dup = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("act-a")),
            MatchExpression::makeLeaf(Field::Activity, Operator::Equals, QStringLiteral("act-b")),
        });
        QString sid;
        int desk = 0;
        QString act;
        QVERIFY(!CRB::contextDimsOf(dup, sid, desk, act));
        QVERIFY(act.isEmpty());
    }

    void testContextDimsOf_refusesDuplicateVirtualDesktopLeaf()
    {
        // Symmetric to the Screen / Activity duplicate-leaf refusals —
        // covers the third of the three context dimensions so the guard
        // is exhaustive against the field axis.
        const MatchExpression dup = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 1),
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 2),
        });
        QString sid;
        int desk = 0;
        QString act;
        QVERIFY(!CRB::contextDimsOf(dup, sid, desk, act));
        QCOMPARE(desk, 0);
    }

    void testContextDimsOf_refusesMalformedVirtualDesktopValue()
    {
        // The value guard, sibling of the three duplicate-leaf guards.
        // QVariant::toInt() turns "abc" into 0 and 0 is the bridge's "no
        // desktop pinned" reading, so without the guard a hand-edited rule
        // would silently reclassify from the Desktop axis to the Monitor one
        // and the batch writers would rewrite it against the wrong key.
        for (const QVariant& bad : {QVariant(QStringLiteral("abc")), QVariant(0), QVariant(-3), QVariant(QString())}) {
            const MatchExpression m = MatchExpression::makeAll({
                MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
                MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, bad),
            });
            QString sid = QStringLiteral("stale");
            int desk = 4;
            QString act = QStringLiteral("stale");
            QVERIFY2(!CRB::contextDimsOf(m, sid, desk, act), qPrintable(bad.toString()));
            // Refusal clears every out-param, not just the desktop, so a
            // caller that ignores the bool cannot read a half-decoded tuple.
            QVERIFY(sid.isEmpty());
            QCOMPARE(desk, 0);
            QVERIFY(act.isEmpty());
        }

        // Discriminator: the same shape with a strictly-positive desktop
        // decodes, so the refusals above are the value and not the two-leaf
        // All{} wrapper.
        const MatchExpression good = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 2),
        });
        QString sid;
        int desk = 0;
        QString act;
        QVERIFY(CRB::contextDimsOf(good, sid, desk, act));
        QCOMPARE(desk, 2);
    }

    void testContextDimsOf_returnTrueOnSuccess()
    {
        // The non-duplicate happy path must return true so callers that care
        // (and any future audit) can rely on the bool contract.
        const MatchExpression m = CRB::makeContextMatch(QStringLiteral("DP-1"), 2, QString());
        QString sid;
        int desk = 0;
        QString act;
        QVERIFY(CRB::contextDimsOf(m, sid, desk, act));
    }

    // ─── Round-trip: makeDisableRule → disableRuleMode ────────────────────

    void testRoundTrip_disableRuleMode()
    {
        const Rule snap = CRB::makeDisableRule(QStringLiteral("s"), QStringLiteral("DP-1"), 0, QString(),
                                               QStringLiteral("snapping"), CRB::kContextBandBase);
        const auto snapMode = CRB::disableRuleMode(snap);
        QVERIFY(snapMode.has_value());
        QCOMPARE(*snapMode, QStringLiteral("snapping"));

        const Rule tile = CRB::makeDisableRule(QStringLiteral("t"), QStringLiteral("DP-1"), 0, QString(),
                                               QStringLiteral("autotile"), CRB::kContextBandBase);
        const auto tileMode = CRB::disableRuleMode(tile);
        QVERIFY(tileMode.has_value());
        QCOMPARE(*tileMode, QStringLiteral("autotile"));

        // The new mode token also round-trips — the bridge is open-vocabulary
        // and defers wire-token validation to the consumer via
        // PhosphorZones::modeFromWireString.
        const Rule scroll = CRB::makeDisableRule(QStringLiteral("c"), QStringLiteral("DP-1"), 0, QString(),
                                                 QStringLiteral("scrolling"), CRB::kContextBandBase);
        const auto scrollMode = CRB::disableRuleMode(scroll);
        QVERIFY(scrollMode.has_value());
        QCOMPARE(*scrollMode, QStringLiteral("scrolling"));
    }

    void testDisableRuleMode_assignmentRuleIsNotADisableRule()
    {
        // An assignment rule carries no DisableEngine action — nullopt.
        const Rule assign = CRB::makeAssignmentRule(QStringLiteral("a"), QStringLiteral("DP-1"), 0, QString(),
                                                    QStringLiteral("snapping"), QStringLiteral("layout-a"), QString(),
                                                    CRB::kContextBandBase, QString());
        QVERIFY(!CRB::disableRuleMode(assign).has_value());
    }

    void testDisableRuleMode_rejectsSecondDisableAction()
    {
        // Two DisableEngine actions → ambiguous, not a bridge-authored rule.
        Rule rule = CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(),
                                         QStringLiteral("snapping"), CRB::kContextBandBase);
        RuleAction extra;
        extra.type = QString(ActionType::DisableEngine);
        extra.params.insert(ActionParam::Mode, QLatin1String("autotile"));
        rule.actions.append(extra);
        QVERIFY(!CRB::disableRuleMode(rule).has_value());
    }

    void testDisableRuleMode_returnsUnknownTokenVerbatim()
    {
        // The bridge is open-vocabulary: it returns any non-empty mode token
        // verbatim and defers vocabulary validation to the consumer (via
        // PhosphorZones::modeFromWireString, which lives next to the Mode
        // enum). A token the consumer does not recognise is therefore not
        // the bridge's failure — the consumer drops the rule on its end.
        Rule rule = CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(),
                                         QStringLiteral("snapping"), CRB::kContextBandBase);
        rule.actions.first().params.insert(ActionParam::Mode, QLatin1String("bogus"));
        const auto token = CRB::disableRuleMode(rule);
        QVERIFY(token.has_value());
        QCOMPARE(*token, QStringLiteral("bogus"));
    }

    void testDisableRuleMode_rejectsEmptyToken()
    {
        // An empty token is malformed — the bridge does treat it as a hard
        // failure (nullopt) so callers do not need to special-case the
        // string-vs-no-string distinction themselves.
        Rule rule = CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(),
                                         QStringLiteral("snapping"), CRB::kContextBandBase);
        rule.actions.first().params.insert(ActionParam::Mode, QString());
        QVERIFY(!CRB::disableRuleMode(rule).has_value());
    }

    // ─── assignmentRuleIdFor / disableRuleIdFor ───────────────────────────

    /// Pins the deterministic id contract: the id `assignmentRuleIdFor` /
    /// `disableRuleIdFor` derive must agree byte-for-byte with the id the
    /// `make*Rule` factories stamp into a freshly-built rule. Callers that
    /// look up the existing rule by tuple via these helpers depend on the
    /// equality holding across catch-all, exact-pin, and the disable-engine
    /// variant — break it and `RuleSet::ruleById` silently misses on
    /// known-existing rules.
    void testAssignmentAndDisableRuleIdsMatchFactoryIds()
    {
        // makeAssignmentRule's id IS assignmentRuleIdFor's id — every tuple
        // axis covered (Monitor, Combined) so a regression in the id helper
        // can't slip in on a single-dimension lookup. The retired
        // provider-default rule no longer exists, so only the Monitor/Combined
        // axes are covered here.
        const Rule monitorOnly = CRB::makeAssignmentRule(
            QStringLiteral("Monitor"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"),
            QStringLiteral("layout-a"), QString(), CRB::kContextBandBase, QString());
        QCOMPARE(monitorOnly.id, CRB::assignmentRuleIdFor(QStringLiteral("DP-1"), 0, QString()));

        const Rule exact = CRB::makeAssignmentRule(QStringLiteral("Exact"), QStringLiteral("DP-1"), 2,
                                                   QStringLiteral("act-x"), QStringLiteral("autotile"), QString(),
                                                   QStringLiteral("algo-b"), CRB::kContextBandBase, QString());
        QCOMPARE(exact.id, CRB::assignmentRuleIdFor(QStringLiteral("DP-1"), 2, QStringLiteral("act-x")));

        // Disable rule — the helper must carry the mode token so the
        // per-engine disable rules for the same tuple stay distinct.
        const Rule disable = CRB::makeDisableRule(QStringLiteral("D"), QStringLiteral("DP-1"), 2, QString(),
                                                  QStringLiteral("autotile"), CRB::kContextBandBase);
        QCOMPARE(disable.id, CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile")));
        // And the snapping-disable for the same tuple is a different id —
        // documents the mode-token contribution to the v5 key.
        QVERIFY(CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile"))
                != CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("snapping")));
    }

    // ─── contextAxisOf ────────────────────────────────────────────────────

    /// The tuple-classifier branches end-to-end. The axes remain the batch
    /// reader/writer families — a missed branch here would let the daemon
    /// route a desktop-pinned rule into the activity band (or vice versa).
    void testContextAxisOf_allBranches()
    {
        QCOMPARE(CRB::contextAxisOf(QString(), 0, QString()), CRB::ContextAxis::CatchAll);
        // Empty screen id collapses everything to CatchAll regardless of
        // other dimensions — documents the empty-screenId pre-empt at the
        // top of the function.
        QCOMPARE(CRB::contextAxisOf(QString(), 5, QStringLiteral("a")), CRB::ContextAxis::CatchAll);

        QCOMPARE(CRB::contextAxisOf(QStringLiteral("DP-1"), 0, QString()), CRB::ContextAxis::Monitor);
        QCOMPARE(CRB::contextAxisOf(QStringLiteral("DP-1"), 3, QString()), CRB::ContextAxis::Desktop);
        QCOMPARE(CRB::contextAxisOf(QStringLiteral("DP-1"), 0, QStringLiteral("act-x")), CRB::ContextAxis::Activity);
        QCOMPARE(CRB::contextAxisOf(QStringLiteral("DP-1"), 3, QStringLiteral("act-x")), CRB::ContextAxis::Combined);
    }

    // ─── matchIsExactContext* predicates ──────────────────────────────────

    /// Per-axis classifiers — one positive and one negative per predicate.
    /// The negatives use the *adjacent* axis (Desktop vs Monitor, Activity vs
    /// Desktop, Combined vs Activity) so a regression that off-by-one's the
    /// switch is caught instead of falling into a "wrong axis but still
    /// kinda close" bucket.
    void testMatchIsExactContextPredicates()
    {
        // matchIsExactContextBase — screen-only ⇒ true; screen+desktop ⇒ false.
        const MatchExpression monitor = CRB::makeContextMatch(QStringLiteral("DP-1"), 0, QString());
        const MatchExpression desktop = CRB::makeContextMatch(QStringLiteral("DP-1"), 3, QString());
        QVERIFY(CRB::matchIsExactContextBase(monitor));
        QVERIFY(!CRB::matchIsExactContextBase(desktop));

        // matchIsExactContextDesktop — screen+desktop ⇒ true; monitor ⇒ false.
        QVERIFY(CRB::matchIsExactContextDesktop(desktop));
        QVERIFY(!CRB::matchIsExactContextDesktop(monitor));

        // matchIsExactContextActivity — Activity AND Combined both ⇒ true.
        // The historical per-activity family deliberately includes the exact-pin
        // shape, so test BOTH axes positive.
        const MatchExpression activity = CRB::makeContextMatch(QStringLiteral("DP-1"), 0, QStringLiteral("act-x"));
        const MatchExpression combined = CRB::makeContextMatch(QStringLiteral("DP-1"), 3, QStringLiteral("act-x"));
        QVERIFY(CRB::matchIsExactContextActivity(activity));
        QVERIFY(CRB::matchIsExactContextActivity(combined));
        QVERIFY(!CRB::matchIsExactContextActivity(desktop));

        // matchIsExactContext (parametric) — positive on the exact tuple,
        // negative on a mismatched desktop number.
        QVERIFY(CRB::matchIsExactContext(combined, QStringLiteral("DP-1"), 3, QStringLiteral("act-x")));
        QVERIFY(!CRB::matchIsExactContext(combined, QStringLiteral("DP-1"), 4, QStringLiteral("act-x")));
    }

    /// The STRICT pair the batch readers/writers use. The broad
    /// matchIsExactContextActivity above admits Combined as well, so these two
    /// are what keep the Activity batch API ((screen, activity) → layout) from
    /// projecting a desktop-pinned Combined rule through a key that cannot
    /// represent it. Combined is therefore the discriminating negative for
    /// Strict, and Activity the discriminating negative for Combined.
    void testMatchIsExactContextStrictAndCombined()
    {
        const MatchExpression monitor = CRB::makeContextMatch(QStringLiteral("DP-1"), 0, QString());
        const MatchExpression desktop = CRB::makeContextMatch(QStringLiteral("DP-1"), 3, QString());
        const MatchExpression activity = CRB::makeContextMatch(QStringLiteral("DP-1"), 0, QStringLiteral("act-x"));
        const MatchExpression combined = CRB::makeContextMatch(QStringLiteral("DP-1"), 3, QStringLiteral("act-x"));

        QVERIFY(CRB::matchIsExactContextActivityStrict(activity));
        QVERIFY(!CRB::matchIsExactContextActivityStrict(combined));
        QVERIFY(!CRB::matchIsExactContextActivityStrict(desktop));
        QVERIFY(!CRB::matchIsExactContextActivityStrict(monitor));

        QVERIFY(CRB::matchIsExactContextCombined(combined));
        QVERIFY(!CRB::matchIsExactContextCombined(activity));
        QVERIFY(!CRB::matchIsExactContextCombined(desktop));
        QVERIFY(!CRB::matchIsExactContextCombined(monitor));

        // The two strict families are disjoint, which is the property the
        // batch round-trip relies on: no rule is claimed by both.
        for (const MatchExpression& m : {monitor, desktop, activity, combined}) {
            QVERIFY(!(CRB::matchIsExactContextActivityStrict(m) && CRB::matchIsExactContextCombined(m)));
        }
    }

    // ─── remapContextRules: a desktop or an activity edit ─────────────────

    void testRemap_desktopRemoval_dropsTheRemovedDesktopsRules()
    {
        const QString s = QStringLiteral("DP-1");
        const QList<Rule> rules{
            CRB::makeAssignmentRule(QString(), s, 1, QString(), QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 2, QString(), QStringLiteral("autotile"), QString(),
                                    QStringLiteral("bsp"), CRB::kContextBandBase, QString()),
            CRB::makeDisableRule(QString(), s, 2, QString(), QStringLiteral("snapping"), CRB::kContextBandBase),
        };
        const auto remapped = CRB::remapContextRules(rules, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 1);
        QCOMPARE(remapped->first().id, rules.first().id);
    }

    void testRemap_desktopRemoval_shiftsHigherDesktopsAndReMintsIds()
    {
        const QString s = QStringLiteral("DP-1");
        const QString act = QStringLiteral("act-a");
        const QList<Rule> rules{
            CRB::makeAssignmentRule(CRB::contextRuleName(s, 3, QString()), s, 3, QString(), QStringLiteral("snapping"),
                                    kLayoutA, QString(), CRB::kContextBandBase, QString()),
            CRB::makeDisableRule(CRB::contextRuleName(s, 3, QString()), s, 3, QString(), QStringLiteral("autotile"),
                                 CRB::kContextBandBase),
            CRB::makeAssignmentRule(QString(), s, 4, act, QStringLiteral("snapping"), kLayoutB, QString(),
                                    CRB::kContextBandBase, QString()),
        };
        const auto remapped = CRB::remapContextRules(rules, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 3);

        const Rule& assignment = remapped->at(0);
        QCOMPARE(assignment.id, CRB::assignmentRuleIdFor(s, 2, QString()));
        QCOMPARE(assignment.name, CRB::contextRuleName(s, 2, QString()));
        QVERIFY(CRB::matchIsExactContext(assignment.match, s, 2, QString()));
        QVERIFY(assignment.actions == rules.at(0).actions);

        const Rule& disable = remapped->at(1);
        QCOMPARE(disable.id, CRB::disableRuleIdFor(s, 2, QString(), QStringLiteral("autotile")));
        QCOMPARE(disable.name, CRB::contextRuleName(s, 2, QString()));
        QVERIFY(CRB::matchIsExactContext(disable.match, s, 2, QString()));

        const Rule& combined = remapped->at(2);
        QCOMPARE(combined.id, CRB::assignmentRuleIdFor(s, 3, act));
        QVERIFY(CRB::matchIsExactContext(combined.match, s, 3, act));
    }

    /// A minted disable rule's name is its mode prefix before the context
    /// label, and the label follows the desktop as the id does (F1012). A
    /// minted disable rule the user renamed keeps the name.
    void testRemap_mintedDisableRuleRenamesItsDesktop()
    {
        const QString s = QStringLiteral("Virtual-0");
        const QString prefix = QStringLiteral("Autotile off · ");
        const Rule minted = CRB::makeDisableRule(prefix + CRB::contextRuleName(s, 3, QString()), s, 3, QString(),
                                                 QStringLiteral("autotile"), CRB::kContextBandBase);
        const Rule renamed = CRB::makeDisableRule(QStringLiteral("No tiling for games"), s, 4, QString(),
                                                  QStringLiteral("autotile"), CRB::kContextBandBase);
        const auto remapped = CRB::remapContextRules({minted, renamed}, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 2);
        QCOMPARE(remapped->at(0).name, QStringLiteral("Autotile off · Virtual-0 · Desktop 2"));
        QCOMPARE(remapped->at(0).id, CRB::disableRuleIdFor(s, 2, QString(), QStringLiteral("autotile")));
        QCOMPARE(remapped->at(1).name, renamed.name);
        QCOMPARE(remapped->at(1).id, CRB::disableRuleIdFor(s, 3, QString(), QStringLiteral("autotile")));
    }

    /// A reorder moves two rules through each other's numbers. Both survive,
    /// each with the id of its new tuple, and each still carries its own layout.
    void testRemap_reorderSwapsWithoutCollision()
    {
        const QString s = QStringLiteral("DP-1");
        const QList<Rule> rules{
            CRB::makeAssignmentRule(QString(), s, 2, QString(), QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 3, QString(), QStringLiteral("snapping"), kLayoutB, QString(),
                                    CRB::kContextBandBase, QString()),
        };
        const auto swap = [](int d) {
            return d == 2 ? 3 : (d == 3 ? 2 : d);
        };
        const auto remapped = CRB::remapContextRules(rules, {swap, {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 2);
        QCOMPARE(remapped->at(0).id, CRB::assignmentRuleIdFor(s, 3, QString()));
        QVERIFY(remapped->at(0).actions == rules.at(0).actions);
        QCOMPARE(remapped->at(1).id, CRB::assignmentRuleIdFor(s, 2, QString()));
        QVERIFY(remapped->at(1).actions == rules.at(1).actions);
    }

    /// The settings app's "layout on virtual desktop" template writes the exact
    /// context shape with a random id and a name of its own. It follows its
    /// desktop like a minted rule but keeps both.
    void testRemap_handBuiltContextRuleKeepsIdAndName()
    {
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("My desktop");
        rule.priority = CRB::kContextBandBase;
        rule.match = MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 3);
        rule.actions = CRB::makeAssignmentActions(QStringLiteral("snapping"), kLayoutA, QString(), QString());
        const auto remapped = CRB::remapContextRules({rule}, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 1);
        QCOMPARE(remapped->first().id, rule.id);
        QCOMPARE(remapped->first().name, rule.name);
        QVERIFY(remapped->first().match == MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 2));
    }

    /// A context match with a Mode leaf is not one context's setting. Its
    /// desktop leaf moves and the Mode leaf stays; naming the removed desktop
    /// switches it off instead of deleting it.
    void testRemap_contextMatchWithAModeLeafFollowsLikeAWindowRule()
    {
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.priority = CRB::kContextBandBase;
        const MatchExpression modeLeaf =
            MatchExpression::makeLeaf(Field::Mode, Operator::Equals, QStringLiteral("snapping"));
        rule.match = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 3),
            modeLeaf,
        });
        rule.actions = CRB::makeAssignmentActions(QStringLiteral("snapping"), kLayoutA, QString(), QString());

        const auto moved = CRB::remapContextRules({rule}, {removal(2), {}});
        QVERIFY(moved);
        QCOMPARE(moved->size(), 1);
        QCOMPARE(moved->first().id, rule.id);
        QVERIFY(moved->first().enabled);
        QVERIFY(moved->first().match.children().at(1) == desktopLeaf(2));
        QVERIFY(moved->first().match.children().at(2) == modeLeaf);

        const auto gone = CRB::remapContextRules({rule}, {removal(3), {}});
        QVERIFY(gone);
        QCOMPARE(gone->size(), 1);
        QVERIFY(!gone->first().enabled);
        QVERIFY(gone->first().match == rule.match);
    }

    /// l18_desktop_rules_renumber: a window rule follows the desktops it names,
    /// in its match and in its RouteToDesktop target.
    void testRemap_windowRuleFollowsItsDesktop()
    {
        const Rule rule = windowRule(MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 3), 4);
        const auto remapped = CRB::remapContextRules({rule}, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 1);
        const Rule& out = remapped->first();
        QCOMPARE(out.id, rule.id);
        QVERIFY(out.enabled);
        QVERIFY(out.match.children().at(1) == desktopLeaf(2));
        QCOMPARE(out.actions.first().params.value(ActionParam::TargetDesktop).toInt(), 3);
    }

    /// F1045: the late start-up renumber reaches only the rules start-up saw.
    /// A held rule, written since in the live numbering, stays as written, even
    /// on the desktop the map removes, and a moved rule landing on a held
    /// rule's id gives way to it.
    void testRemap_appliesHoldsRulesAndAHeldRuleOutranksAMovedOne()
    {
        const QString s = QStringLiteral("DP-1");
        const Rule restored = CRB::makeAssignmentRule(QString(), s, 3, QString(), QStringLiteral("snapping"), kLayoutA,
                                                      QString(), CRB::kContextBandBase, QString());
        const Rule restoredWindow = windowRule(desktopLeaf(3), 0);
        const Rule heldOnTwo = CRB::makeAssignmentRule(QString(), s, 2, QString(), QStringLiteral("snapping"), kLayoutB,
                                                       QString(), CRB::kContextBandBase, QString());
        const Rule heldWindow = windowRule(desktopLeaf(3), 0);
        const QSet<QUuid> held{heldOnTwo.id, heldWindow.id};
        CRB::ContextRemap remap{removal(2), {}, [&held](const Rule& rule) {
                                    return !held.contains(rule.id);
                                }};

        const auto remapped = CRB::remapContextRules({restored, restoredWindow, heldOnTwo, heldWindow}, remap);
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 3);
        QCOMPARE(remapped->at(0).id, restoredWindow.id);
        QVERIFY(remapped->at(0).match.children().at(1) == desktopLeaf(2));
        QVERIFY(remapped->at(1) == heldOnTwo);
        QVERIFY(remapped->at(2) == heldWindow);
    }

    /// l18_desktop_rules_renumber: a window rule that named the removed desktop
    /// is switched off, not deleted, and keeps what it said. A negated leaf
    /// names the desktop too, and so does the RouteToDesktop target alone.
    void testRemap_windowRuleNamingTheRemovedDesktopIsSwitchedOff()
    {
        const Rule negated = windowRule(
            MatchExpression::makeNone({MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, 2)}), 0);
        const Rule routed = windowRule(MatchExpression(), 2);
        const auto remapped = CRB::remapContextRules({negated, routed}, {removal(2), {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 2);
        QVERIFY(!remapped->at(0).enabled);
        QVERIFY(remapped->at(0).match == negated.match);
        QVERIFY(!remapped->at(1).enabled);
        QVERIFY(remapped->at(1).actions == routed.actions);
    }

    /// l18_d_q1_rules_confirm: a user rule whose only condition is a desktop
    /// has the context shape, but a window action makes it a window rule. It
    /// follows a desktop that moved and is switched off, not deleted, when its
    /// desktop goes (F1092).
    void testRemap_desktopOnlyWindowRuleIsSwitchedOffNotDropped()
    {
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("Float on desktop 3");
        rule.priority = 500;
        rule.match = desktopLeaf(3);
        RuleAction floatAction;
        floatAction.type = QString(ActionType::Float);
        rule.actions.append(floatAction);

        const auto moved = CRB::remapContextRules({rule}, {removal(2), {}});
        QVERIFY(moved);
        QCOMPARE(moved->size(), 1);
        QCOMPARE(moved->first().id, rule.id);
        QVERIFY(moved->first().enabled);
        QVERIFY(moved->first().match == desktopLeaf(2));

        const auto gone = CRB::remapContextRules({rule}, {removal(3), {}});
        QVERIFY(gone);
        QCOMPARE(gone->size(), 1);
        QCOMPARE(gone->first().id, rule.id);
        QVERIFY(!gone->first().enabled);
        QVERIFY(gone->first().match == rule.match);
    }

    /// A range has no rewrite that names the same desktops after a reorder,
    /// so it is left as written; so is a rule below the removed desktop and a
    /// monitor-only one, and then nothing changed at all.
    void testRemap_untouchedRulesAreNullopt()
    {
        const QString s = QStringLiteral("DP-1");
        const QList<Rule> rules{
            windowRule(MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::GreaterThan, 2), 0),
            CRB::makeAssignmentRule(QString(), s, 1, QString(), QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 0, QString(), QStringLiteral("snapping"), kLayoutB, QString(),
                                    CRB::kContextBandBase, QString()),
        };
        QVERIFY(!CRB::remapContextRules(rules, {removal(2), {}}));
        QVERIFY(!CRB::remapContextRules(rules, {}));
    }

    /// The count sweep: every desktop past the count is gone.
    void testRemap_countDropsRulesPastIt()
    {
        const QString s = QStringLiteral("DP-1");
        const QList<Rule> rules{
            CRB::makeAssignmentRule(QString(), s, 3, QString(), QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 4, QString(), QStringLiteral("snapping"), kLayoutB, QString(),
                                    CRB::kContextBandBase, QString()),
        };
        const auto threeLeft = [](int d) {
            return d > 3 ? 0 : d;
        };
        const auto remapped = CRB::remapContextRules(rules, {threeLeft, {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 1);
        QCOMPARE(remapped->first().id, rules.first().id);
    }

    /// A context rule pinned to a removed activity goes, Combined included; a
    /// live activity's stays, and a window rule naming an activity is left alone.
    void testRemap_activityDropsRulesOfAGoneActivity()
    {
        const QString s = QStringLiteral("DP-1");
        const QString live = QStringLiteral("act-live");
        const QString gone = QStringLiteral("act-gone");
        const Rule windowOnGone = windowRule(MatchExpression::makeLeaf(Field::Activity, Operator::Equals, gone), 0);
        const QList<Rule> rules{
            CRB::makeAssignmentRule(QString(), s, 0, gone, QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 2, gone, QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeDisableRule(QString(), s, 0, live, QStringLiteral("snapping"), CRB::kContextBandBase),
            windowOnGone,
        };
        const auto remapped = CRB::remapContextRules(rules, {{}, [&live](const QString& a) {
                                                                 return a == live;
                                                             }});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 2);
        QCOMPARE(remapped->at(0).id, rules.at(2).id);
        QVERIFY(remapped->at(1) == windowOnGone);
    }

    /// A malformed desktop value is refused by contextDimsOf and has no number
    /// to move, so the rule is left exactly as it is.
    void testRemap_malformedDesktopValueIsLeftAlone_data()
    {
        QTest::addColumn<QVariant>("bad");
        QTest::newRow("not a number") << QVariant(QStringLiteral("abc"));
        QTest::newRow("zero") << QVariant(0);
        QTest::newRow("negative") << QVariant(-3);
    }

    void testRemap_malformedDesktopValueIsLeftAlone()
    {
        QFETCH(QVariant, bad);
        Rule rule = CRB::makeAssignmentRule(QString(), QStringLiteral("DP-1"), 3, QString(), QStringLiteral("snapping"),
                                            kLayoutA, QString(), CRB::kContextBandBase, QString());
        rule.match = MatchExpression::makeAll({
            MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("DP-1")),
            MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, bad),
        });
        QVERIFY(!CRB::remapContextRules({rule}, {removal(2), {}}));
    }

    /// A desktop inserted at the front moves desktop 2 onto 3. A stale rule
    /// already at 3, past the old two-desktop list, is not the map's to move
    /// and would now share the moved rule's id: the moved one is kept.
    void testRemap_staleRuleOnAMovedIdIsDropped()
    {
        const QString s = QStringLiteral("DP-1");
        const QList<Rule> rules{
            CRB::makeAssignmentRule(QString(), s, 3, QString(), QStringLiteral("snapping"), kLayoutB, QString(),
                                    CRB::kContextBandBase, QString()),
            CRB::makeAssignmentRule(QString(), s, 2, QString(), QStringLiteral("snapping"), kLayoutA, QString(),
                                    CRB::kContextBandBase, QString()),
        };
        const auto insertAtFront = [](int d) {
            return d >= 1 && d <= 2 ? d + 1 : d;
        };
        const auto remapped = CRB::remapContextRules(rules, {insertAtFront, {}});
        QVERIFY(remapped);
        QCOMPARE(remapped->size(), 1);
        QCOMPARE(remapped->first().id, CRB::assignmentRuleIdFor(s, 3, QString()));
        QVERIFY(remapped->first().actions == rules.at(1).actions);
    }

private:
    inline static const QString kLayoutA = QStringLiteral("{00000000-0000-0000-0000-00000000000a}");
    inline static const QString kLayoutB = QStringLiteral("{00000000-0000-0000-0000-00000000000b}");

    static MatchExpression desktopLeaf(int desktop)
    {
        return MatchExpression::makeLeaf(Field::VirtualDesktop, Operator::Equals, desktop);
    }

    /// One desktop removed at @p removed: the map the daemon builds for it.
    static std::function<int(int)> removal(int removed)
    {
        return [removed](int d) {
            return d == removed ? 0 : (d > removed ? d - 1 : d);
        };
    }

    /// A window rule: an AppId leaf beside @p extra (none for the catch-all),
    /// routing to @p routeDesktop when it is above 0, else setting the engine
    /// mode to snapping.
    static Rule windowRule(const MatchExpression& extra, int routeDesktop)
    {
        Rule rule;
        rule.id = QUuid::createUuid();
        rule.name = QStringLiteral("Browser");
        rule.priority = 500;
        QList<MatchExpression> leaves{
            MatchExpression::makeLeaf(Field::AppId, Operator::Equals, QStringLiteral("org.example.browser"))};
        if (!extra.isCatchAll()) {
            leaves.append(extra);
        }
        rule.match = MatchExpression::makeAll(leaves);
        RuleAction action;
        if (routeDesktop > 0) {
            action.type = QString(ActionType::RouteToDesktop);
            action.params.insert(ActionParam::TargetDesktop, routeDesktop);
        } else {
            action.type = QString(ActionType::SetEngineMode);
            action.params.insert(ActionParam::Mode, QStringLiteral("snapping"));
        }
        rule.actions.append(action);
        return rule;
    }
};

QTEST_GUILESS_MAIN(TestContextRuleBridge)
#include "test_contextrulebridge.moc"

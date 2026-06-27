// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_contextrulebridge.cpp
 * @brief Unit tests for the header-only ContextRuleBridge.
 *
 * Exercises every bridge function: contextPriority (incl. the all-false → 0
 * branch), makeContextMatch (single-leaf collapse vs makeAll vs catch-all),
 * makeAssignmentActions / makeAssignmentRule (three-action losslessness +
 * empty-field omission), makeProviderDefaultRule, makeDisableRule, and the
 * makeContextMatch → contextDimsOf / disableRuleMode round-trips.
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

    // ─── contextPriority ──────────────────────────────────────────────────

    void testContextPriority_allFalseIsZero()
    {
        // A rule that pins nothing is the provider default — priority 0.
        QCOMPARE(CRB::contextPriority(false, false, false), CRB::kProviderDefaultPriority);
        QCOMPARE(CRB::contextPriority(false, false, false), 0);
    }

    void testContextPriority_cascadeBands()
    {
        // Screen only — display default.
        QCOMPARE(CRB::contextPriority(true, false, false), 310);
        // Screen + desktop.
        QCOMPARE(CRB::contextPriority(true, true, false), 410);
        // Screen + activity.
        QCOMPARE(CRB::contextPriority(true, false, true), 510);
        // Exact — screen + desktop + activity.
        QCOMPARE(CRB::contextPriority(true, true, true), 610);
    }

    void testContextPriority_activityBeatsDesktop()
    {
        // The activity weight must strictly exceed the desktop weight so an
        // activity-pinned rule always outranks a desktop-pinned one.
        QVERIFY(CRB::contextPriority(true, false, true) > CRB::contextPriority(true, true, false));
    }

    void testContextPriority_pinnedWithoutScreen()
    {
        // A desktop- or activity-only pin still lands in the pinned band
        // (>= kBasePriority), strictly above the provider default.
        QVERIFY(CRB::contextPriority(false, true, false) > CRB::kProviderDefaultPriority);
        QVERIFY(CRB::contextPriority(false, false, true) > CRB::kProviderDefaultPriority);
    }

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
            QStringLiteral("snapping"), QStringLiteral("layout-a"), QStringLiteral("algo-b"));
        // Mode + layout + algorithm — both layout fields survive a mode flip.
        QCOMPARE(actions.size(), 3);
        QCOMPARE(modeToken(actions), QStringLiteral("snapping"));
        QCOMPARE(actionCount(actions, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(actions, ActionType::SetTilingAlgorithm), 1);
    }

    void testMakeAssignmentActions_autotileModeToken()
    {
        const QList<RuleAction> actions = CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QString());
        QCOMPARE(modeToken(actions), QStringLiteral("autotile"));
    }

    void testMakeAssignmentActions_emptyFieldsOmitted()
    {
        // Mode-only entry — both layout fields empty → a single action.
        const QList<RuleAction> modeOnly = CRB::makeAssignmentActions(QStringLiteral("autotile"), QString(), QString());
        QCOMPARE(modeOnly.size(), 1);
        QCOMPARE(modeOnly.first().type, QString(ActionType::SetEngineMode));

        // Layout but no algorithm.
        const QList<RuleAction> layoutOnly =
            CRB::makeAssignmentActions(QStringLiteral("snapping"), QStringLiteral("layout-a"), QString());
        QCOMPARE(layoutOnly.size(), 2);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetTilingAlgorithm), 0);
    }

    // ─── makeAssignmentRule ───────────────────────────────────────────────

    void testMakeAssignmentRule_isValidAndContextOnly()
    {
        const Rule rule =
            CRB::makeAssignmentRule(QStringLiteral("Exact rule"), QStringLiteral("DP-1"), 2, QStringLiteral("act-x"),
                                    QStringLiteral("snapping"), QStringLiteral("layout-a"), QStringLiteral("algo-b"));
        QVERIFY(rule.isValid());
        QVERIFY(!rule.id.isNull());
        QVERIFY(rule.match.isContextOnly());
        QCOMPARE(rule.name, QStringLiteral("Exact rule"));
        // Exact pin → priority 610.
        QCOMPARE(rule.priority, 610);
        QCOMPARE(rule.actions.size(), 3);
    }

    void testMakeAssignmentRule_modeOnlyEntry()
    {
        const Rule rule = CRB::makeAssignmentRule(QStringLiteral("Display default"), QStringLiteral("HDMI-1"), 0,
                                                  QString(), QStringLiteral("autotile"), QString(), QString());
        QVERIFY(rule.isValid());
        QCOMPARE(rule.priority, 310); // screen only
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(modeToken(rule.actions), QStringLiteral("autotile"));
    }

    // ─── makeProviderDefaultRule ──────────────────────────────────────────

    void testMakeProviderDefaultRule_isCatchAllAtZero()
    {
        const Rule rule = CRB::makeProviderDefaultRule(QStringLiteral("Global default"), QStringLiteral("snapping"),
                                                       QStringLiteral("layout-a"), QString());
        QVERIFY(rule.isValid());
        QVERIFY(rule.match.isCatchAll());
        QCOMPARE(rule.priority, CRB::kProviderDefaultPriority);
        QCOMPARE(modeToken(rule.actions), QStringLiteral("snapping"));
    }

    // ─── makeDisableRule ──────────────────────────────────────────────────

    void testMakeDisableRule_carriesSingleDisableEngineAction()
    {
        const Rule rule = CRB::makeDisableRule(QStringLiteral("Snapping off · DP-1"), QStringLiteral("DP-1"), 0,
                                               QString(), QStringLiteral("snapping"));
        QVERIFY(rule.isValid());
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(rule.actions.first().type, QString(ActionType::DisableEngine));
        QCOMPARE(rule.priority, 310); // screen only
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
        const Rule snap =
            CRB::makeDisableRule(QStringLiteral("s"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
        const auto snapMode = CRB::disableRuleMode(snap);
        QVERIFY(snapMode.has_value());
        QCOMPARE(*snapMode, QStringLiteral("snapping"));

        const Rule tile =
            CRB::makeDisableRule(QStringLiteral("t"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("autotile"));
        const auto tileMode = CRB::disableRuleMode(tile);
        QVERIFY(tileMode.has_value());
        QCOMPARE(*tileMode, QStringLiteral("autotile"));

        // The new mode token also round-trips — the bridge is open-vocabulary
        // and defers wire-token validation to the consumer via
        // PhosphorZones::modeFromWireString.
        const Rule scroll = CRB::makeDisableRule(QStringLiteral("c"), QStringLiteral("DP-1"), 0, QString(),
                                                 QStringLiteral("scrolling"));
        const auto scrollMode = CRB::disableRuleMode(scroll);
        QVERIFY(scrollMode.has_value());
        QCOMPARE(*scrollMode, QStringLiteral("scrolling"));
    }

    void testDisableRuleMode_assignmentRuleIsNotADisableRule()
    {
        // An assignment rule carries no DisableEngine action — nullopt.
        const Rule assign = CRB::makeAssignmentRule(QStringLiteral("a"), QStringLiteral("DP-1"), 0, QString(),
                                                    QStringLiteral("snapping"), QStringLiteral("layout-a"), QString());
        QVERIFY(!CRB::disableRuleMode(assign).has_value());
    }

    void testDisableRuleMode_rejectsSecondDisableAction()
    {
        // Two DisableEngine actions → ambiguous, not a bridge-authored rule.
        Rule rule =
            CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
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
        Rule rule =
            CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
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
        Rule rule =
            CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(), QStringLiteral("snapping"));
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
        // can't slip in on a single-dimension lookup. The provider default is
        // a separate family ("provider-default"), so it intentionally is NOT
        // included here — that's covered by makeProviderDefaultRule_isCatchAllAtZero.
        const Rule monitorOnly =
            CRB::makeAssignmentRule(QStringLiteral("Monitor"), QStringLiteral("DP-1"), 0, QString(),
                                    QStringLiteral("snapping"), QStringLiteral("layout-a"), QString());
        QCOMPARE(monitorOnly.id, CRB::assignmentRuleIdFor(QStringLiteral("DP-1"), 0, QString()));

        const Rule exact =
            CRB::makeAssignmentRule(QStringLiteral("Exact"), QStringLiteral("DP-1"), 2, QStringLiteral("act-x"),
                                    QStringLiteral("autotile"), QString(), QStringLiteral("algo-b"));
        QCOMPARE(exact.id, CRB::assignmentRuleIdFor(QStringLiteral("DP-1"), 2, QStringLiteral("act-x")));

        // Disable rule — the helper must carry the mode token so the
        // per-engine disable rules for the same tuple stay distinct.
        const Rule disable =
            CRB::makeDisableRule(QStringLiteral("D"), QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile"));
        QCOMPARE(disable.id, CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile")));
        // And the snapping-disable for the same tuple is a different id —
        // documents the mode-token contribution to the v5 key.
        QVERIFY(CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("autotile"))
                != CRB::disableRuleIdFor(QStringLiteral("DP-1"), 2, QString(), QStringLiteral("snapping")));
    }

    // ─── contextAxisOf ────────────────────────────────────────────────────

    /// The tuple-classifier branches end-to-end. Mirrors `contextPriority`'s
    /// pinned-dimensions formula — a missed branch here would let the daemon
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
};

QTEST_GUILESS_MAIN(TestContextRuleBridge)
#include "test_contextrulebridge.moc"

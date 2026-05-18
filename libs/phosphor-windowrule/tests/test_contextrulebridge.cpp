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
 * makeContextMatch → contextDimsOf / disableRuleAutotileMode round-trips.
 */

#include <QTest>

#include <PhosphorWindowRule/ContextRuleBridge.h>
#include <PhosphorWindowRule/MatchExpression.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/WindowRule.h>

using namespace PhosphorWindowRule;
namespace CRB = PhosphorWindowRule::ContextRuleBridge;

class TestContextRuleBridge : public QObject
{
    Q_OBJECT

private:
    // Convenience: the SetEngineMode action's mode token, or empty if absent.
    static QString modeToken(const QList<RuleAction>& actions)
    {
        for (const RuleAction& a : actions) {
            if (a.type == QString(ActionType::SetEngineMode)) {
                return a.params.value(QLatin1String("mode")).toString();
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
        const QList<RuleAction> actions =
            CRB::makeAssignmentActions(/*autotileMode=*/false, QStringLiteral("layout-a"), QStringLiteral("algo-b"));
        // Mode + layout + algorithm — both layout fields survive a mode flip.
        QCOMPARE(actions.size(), 3);
        QCOMPARE(modeToken(actions), QStringLiteral("snapping"));
        QCOMPARE(actionCount(actions, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(actions, ActionType::SetTilingAlgorithm), 1);
    }

    void testMakeAssignmentActions_autotileModeToken()
    {
        const QList<RuleAction> actions = CRB::makeAssignmentActions(/*autotileMode=*/true, QString(), QString());
        QCOMPARE(modeToken(actions), QStringLiteral("autotile"));
    }

    void testMakeAssignmentActions_emptyFieldsOmitted()
    {
        // Mode-only entry — both layout fields empty → a single action.
        const QList<RuleAction> modeOnly = CRB::makeAssignmentActions(true, QString(), QString());
        QCOMPARE(modeOnly.size(), 1);
        QCOMPARE(modeOnly.first().type, QString(ActionType::SetEngineMode));

        // Layout but no algorithm.
        const QList<RuleAction> layoutOnly = CRB::makeAssignmentActions(false, QStringLiteral("layout-a"), QString());
        QCOMPARE(layoutOnly.size(), 2);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetSnappingLayout), 1);
        QCOMPARE(actionCount(layoutOnly, ActionType::SetTilingAlgorithm), 0);
    }

    // ─── makeAssignmentRule ───────────────────────────────────────────────

    void testMakeAssignmentRule_isValidAndContextOnly()
    {
        const WindowRule rule =
            CRB::makeAssignmentRule(QStringLiteral("Exact rule"), QStringLiteral("DP-1"), 2, QStringLiteral("act-x"),
                                    /*autotile=*/false, QStringLiteral("layout-a"), QStringLiteral("algo-b"));
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
        const WindowRule rule = CRB::makeAssignmentRule(QStringLiteral("Display default"), QStringLiteral("HDMI-1"), 0,
                                                        QString(), /*autotile=*/true, QString(), QString());
        QVERIFY(rule.isValid());
        QCOMPARE(rule.priority, 310); // screen only
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(modeToken(rule.actions), QStringLiteral("autotile"));
    }

    // ─── makeProviderDefaultRule ──────────────────────────────────────────

    void testMakeProviderDefaultRule_isCatchAllAtZero()
    {
        const WindowRule rule = CRB::makeProviderDefaultRule(QStringLiteral("Global default"), /*autotile=*/false,
                                                             QStringLiteral("layout-a"), QString());
        QVERIFY(rule.isValid());
        QVERIFY(rule.match.isCatchAll());
        QCOMPARE(rule.priority, CRB::kProviderDefaultPriority);
        QCOMPARE(modeToken(rule.actions), QStringLiteral("snapping"));
    }

    // ─── makeDisableRule ──────────────────────────────────────────────────

    void testMakeDisableRule_carriesSingleDisableEngineAction()
    {
        const WindowRule rule = CRB::makeDisableRule(QStringLiteral("Snapping off · DP-1"), QStringLiteral("DP-1"), 0,
                                                     QString(), /*autotile=*/false);
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
        CRB::contextDimsOf(any, sid, desk, act);
        QVERIFY(sid.isEmpty());
        QCOMPARE(desk, 0);
        QVERIFY(act.isEmpty());
    }

    // ─── Round-trip: makeDisableRule → disableRuleAutotileMode ────────────

    void testRoundTrip_disableRuleAutotileMode()
    {
        const WindowRule snap = CRB::makeDisableRule(QStringLiteral("s"), QStringLiteral("DP-1"), 0, QString(), false);
        const auto snapMode = CRB::disableRuleAutotileMode(snap);
        QVERIFY(snapMode.has_value());
        QCOMPARE(*snapMode, false);

        const WindowRule tile = CRB::makeDisableRule(QStringLiteral("t"), QStringLiteral("DP-1"), 0, QString(), true);
        const auto tileMode = CRB::disableRuleAutotileMode(tile);
        QVERIFY(tileMode.has_value());
        QCOMPARE(*tileMode, true);
    }

    void testDisableRuleAutotileMode_assignmentRuleIsNotADisableRule()
    {
        // An assignment rule carries no DisableEngine action — nullopt.
        const WindowRule assign = CRB::makeAssignmentRule(QStringLiteral("a"), QStringLiteral("DP-1"), 0, QString(),
                                                          false, QStringLiteral("layout-a"), QString());
        QVERIFY(!CRB::disableRuleAutotileMode(assign).has_value());
    }

    void testDisableRuleAutotileMode_rejectsSecondDisableAction()
    {
        // Two DisableEngine actions → ambiguous, not a bridge-authored rule.
        WindowRule rule = CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(), false);
        RuleAction extra;
        extra.type = QString(ActionType::DisableEngine);
        extra.params.insert(QLatin1String("mode"), QLatin1String("autotile"));
        rule.actions.append(extra);
        QVERIFY(!CRB::disableRuleAutotileMode(rule).has_value());
    }

    void testDisableRuleAutotileMode_rejectsUnknownModeToken()
    {
        WindowRule rule = CRB::makeDisableRule(QStringLiteral("d"), QStringLiteral("DP-1"), 0, QString(), false);
        // Corrupt the mode token to an unrecognised value.
        rule.actions.first().params.insert(QLatin1String("mode"), QLatin1String("bogus"));
        QVERIFY(!CRB::disableRuleAutotileMode(rule).has_value());
    }
};

QTEST_MAIN(TestContextRuleBridge)
#include "test_contextrulebridge.moc"

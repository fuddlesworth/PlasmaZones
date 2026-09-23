// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Priority-wins oracle for context-only rules: the highest-priority matching
// rule per slot wins (ties by stable list order). There is no specificity
// formula in the resolver, so this oracle assigns each context rule an explicit
// priority that preserves the legacy cascade ordering
// (exact > screen+activity > screen+desktop > screen-only > catch-all default)
// and proves the RuleEvaluator resolves each query to the highest-priority
// match. The match expression still comes from `ContextRuleBridge`; the
// priorities are now the caller's, chosen here by pinned-dimension count.
//
// Explicit priorities (Context band base = 300):
//   exact (screen+desktop+activity)  306
//   screen + activity                304
//   screen + desktop                 303
//   screen only                      301
//   catch-all default                  1   (empty-All{} match, the floor)

#include "RuleTestHelpers.h"

#include <PhosphorRules/ContextRuleBridge.h>

#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;

namespace {

/// Explicit priority for a context rule, chosen to preserve the legacy cascade
/// ordering under the priority-wins resolver. The resolver no longer derives
/// this; the caller owns it. A rule pinning nothing is the catch-all floor.
int cascadePriority(bool screenPinned, bool desktopPinned, bool activityPinned)
{
    if (!screenPinned && !desktopPinned && !activityPinned) {
        return 1; // catch-all default — the floor
    }
    return ContextRuleBridge::kContextBandBase + (activityPinned ? 3 : 0) + (desktopPinned ? 2 : 0)
        + (screenPinned ? 1 : 0);
}

/// Builds a context-only rule pinning the given dimensions, with one
/// engine-mode action carrying @p tag as its mode so the test can read back
/// which rule won. The match expression comes from the production
/// `ContextRuleBridge`; the priority is assigned locally by `cascadePriority`,
/// since the resolver no longer derives it.
Rule contextRule(const QString& name, const QString& screenId, int desktop, const QString& activity, const QString& tag)
{
    const bool screenPinned = !screenId.isEmpty();
    const bool desktopPinned = desktop > 0;
    const bool activityPinned = !activity.isEmpty();

    const MatchExpression match = ContextRuleBridge::makeContextMatch(screenId, desktop, activity);
    const int priority = cascadePriority(screenPinned, desktopPinned, activityPinned);
    return makeRule(name, priority, match, {engineMode(tag)});
}

QString resolvedMode(const RuleEvaluator& eval, const WindowQuery& q)
{
    const auto action = eval.resolve(q).slot(QString(ActionSlot::EngineMode));
    return action ? action->params.value(QStringLiteral("mode")).toString() : QString();
}

const QString kScreen = QStringLiteral("DP-2");
const QString kWorkActivity = QStringLiteral("{work-uuid}");

} // namespace

class TestRuleEvaluatorCascade : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Full cascade ──
    // The priority ordering itself is proven through the resolver below
    // (highest-priority rule wins per slot); there is no separate "formula"
    // contract to pin now that the resolver derives nothing.

    void testExactBeatsEverything()
    {
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        set.addRule(
            contextRule(QStringLiteral("screen+desktop"), kScreen, 2, QString(), QStringLiteral("screen+desktop")));
        set.addRule(contextRule(QStringLiteral("screen+activity"), kScreen, 0, kWorkActivity,
                                QStringLiteral("screen+activity")));
        set.addRule(contextRule(QStringLiteral("exact"), kScreen, 2, kWorkActivity, QStringLiteral("exact")));
        RuleEvaluator eval(set);

        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 2;
        q.activity = kWorkActivity;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("exact"));
    }

    void testActivityBeatsDesktopInResolution()
    {
        RuleSet set;
        set.addRule(
            contextRule(QStringLiteral("screen+desktop"), kScreen, 2, QString(), QStringLiteral("screen+desktop")));
        set.addRule(contextRule(QStringLiteral("screen+activity"), kScreen, 0, kWorkActivity,
                                QStringLiteral("screen+activity")));
        RuleEvaluator eval(set);

        // A window where BOTH the desktop rule and the activity rule match —
        // activity must win.
        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 2;
        q.activity = kWorkActivity;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen+activity"));
    }

    void testDesktopBeatsScreenOnly()
    {
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        set.addRule(
            contextRule(QStringLiteral("screen+desktop"), kScreen, 2, QString(), QStringLiteral("screen+desktop")));
        RuleEvaluator eval(set);

        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 2;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen+desktop"));
    }

    void testScreenOnlyBeatsCatchAllDefault()
    {
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 5;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen"));
    }

    void testFallThroughToCatchAllDefault()
    {
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        // A query on a screen with no specific rule falls through to the
        // empty-All{} catch-all default.
        WindowQuery q;
        q.screenId = QStringLiteral("HDMI-9");
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("default"));
    }

    void testContextRuleMatchesWindowlessQuery()
    {
        // The cascade is resolved with a windowless context query — no
        // window attributes present. Context-only rules must still match.
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        WindowQuery q; // no window attributes
        q.screenId = kScreen;
        QVERIFY(!q.hasWindow());
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen"));
    }

    void testWindowPropertyRuleInertDuringContextResolution()
    {
        // A window-property rule (windowClass) must NOT fire for a windowless
        // context query — that is the cascade-fidelity invariant.
        RuleSet set;
        set.addRule(
            makeRule(QStringLiteral("firefox"), 9999,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("firefox")),
                     {engineMode(QStringLiteral("window-rule"))}));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        WindowQuery q; // windowless
        q.screenId = kScreen;
        // Despite the firefox rule's far higher priority, it cannot match a
        // query with no windowClass — the context rule wins.
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen"));
    }

    void testFullCascadeWalk()
    {
        // One rule per cascade level; verify each query level resolves to its
        // own band.
        RuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        set.addRule(
            contextRule(QStringLiteral("screen+desktop"), kScreen, 2, QString(), QStringLiteral("screen+desktop")));
        set.addRule(contextRule(QStringLiteral("screen+activity"), kScreen, 0, kWorkActivity,
                                QStringLiteral("screen+activity")));
        set.addRule(contextRule(QStringLiteral("exact"), kScreen, 2, kWorkActivity, QStringLiteral("exact")));
        RuleEvaluator eval(set);

        // Exact context.
        WindowQuery exact;
        exact.screenId = kScreen;
        exact.virtualDesktop = 2;
        exact.activity = kWorkActivity;
        QCOMPARE(resolvedMode(eval, exact), QStringLiteral("exact"));

        // Screen + activity (desktop with no specific rule).
        WindowQuery scrAct;
        scrAct.screenId = kScreen;
        scrAct.virtualDesktop = 7;
        scrAct.activity = kWorkActivity;
        QCOMPARE(resolvedMode(eval, scrAct), QStringLiteral("screen+activity"));

        // Screen + desktop (activity with no specific rule).
        WindowQuery scrDesk;
        scrDesk.screenId = kScreen;
        scrDesk.virtualDesktop = 2;
        scrDesk.activity = QStringLiteral("{some-other-activity}");
        QCOMPARE(resolvedMode(eval, scrDesk), QStringLiteral("screen+desktop"));

        // Screen only.
        WindowQuery scrOnly;
        scrOnly.screenId = kScreen;
        scrOnly.virtualDesktop = 9;
        scrOnly.activity = QStringLiteral("{unknown}");
        QCOMPARE(resolvedMode(eval, scrOnly), QStringLiteral("screen"));

        // Unknown screen -> catch-all default.
        WindowQuery def;
        def.screenId = QStringLiteral("VGA-3");
        QCOMPARE(resolvedMode(eval, def), QStringLiteral("default"));
    }
};

QTEST_GUILESS_MAIN(TestRuleEvaluatorCascade)
#include "test_ruleevaluator_cascade.moc"

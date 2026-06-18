// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Cascade-fidelity oracle. Phase 1 writes this NOW so Phase 3's migration has
// a behavioural reference: it proves that context-only rules, ordered by the
// production `ContextRuleBridge` priority formula, reproduce the existing
// zone-Assignment cascade
// (exact -> activity -> desktop -> screen -> provider-default) exactly.
//
// The priority formula and the context match expression are NOT re-derived
// here — both are delegated to `ContextRuleBridge` so this oracle exercises
// the real production code path. `testPriorityBands` pins the literal bands
// the bridge must keep producing.
//
// Priority bands:
//   exact (screen+desktop+activity)  610
//   screen + activity                510   (activity weight 200 > desktop 100)
//   screen + desktop                 410
//   screen only                      310
//   provider default                   0   (empty-All{} catch-all)

#include "RuleTestHelpers.h"

#include <PhosphorWindowRules/ContextRuleBridge.h>

#include <QTest>

using namespace PhosphorWindowRules;
using namespace PhosphorWindowRules::TestHelpers;

namespace {

/// The cascade -> priority formula under test. Delegated to the production
/// `ContextRuleBridge` so this oracle exercises the *real* formula rather than
/// a hand-rolled copy that could silently drift from it.
int cascadePriority(bool screenPinned, bool desktopPinned, bool activityPinned)
{
    return ContextRuleBridge::contextPriority(screenPinned, desktopPinned, activityPinned);
}

/// Builds a context-only rule pinning the given dimensions, with one
/// engine-mode action carrying @p tag as its mode so the test can read back
/// which rule won. The match expression and priority both come from the
/// production `ContextRuleBridge` — the cascade fidelity this file asserts is
/// the bridge's behaviour, not a re-implementation.
WindowRule contextRule(const QString& name, const QString& screenId, int desktop, const QString& activity,
                       const QString& tag)
{
    const bool screenPinned = !screenId.isEmpty();
    const bool desktopPinned = desktop > 0;
    const bool activityPinned = !activity.isEmpty();

    const MatchExpression match = ContextRuleBridge::makeContextMatch(screenId, desktop, activity);
    const int priority = ContextRuleBridge::contextPriority(screenPinned, desktopPinned, activityPinned);
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

    // ── Priority formula sanity ──

    void testPriorityBands()
    {
        // Assert the production bridge formula reproduces the literal cascade
        // bands. These constants are the cascade-fidelity contract — if the
        // bridge ever changes its weights, this oracle must fail loudly.
        QCOMPARE(ContextRuleBridge::contextPriority(true, true, true), 610); // exact
        QCOMPARE(ContextRuleBridge::contextPriority(true, false, true), 510); // screen + activity
        QCOMPARE(ContextRuleBridge::contextPriority(true, true, false), 410); // screen + desktop
        QCOMPARE(ContextRuleBridge::contextPriority(true, false, false), 310); // screen only
        QCOMPARE(ContextRuleBridge::contextPriority(false, false, false), 0); // provider default
    }

    void testActivityBeatsDesktop_structurally()
    {
        // The whole "activity beats desktop" guarantee is the weight ordering.
        QVERIFY(cascadePriority(true, false, true) > cascadePriority(true, true, false));
    }

    // ── Full cascade ──

    void testExactBeatsEverything()
    {
        WindowRuleSet set;
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
        WindowRuleSet set;
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
        WindowRuleSet set;
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        set.addRule(
            contextRule(QStringLiteral("screen+desktop"), kScreen, 2, QString(), QStringLiteral("screen+desktop")));
        RuleEvaluator eval(set);

        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 2;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen+desktop"));
    }

    void testScreenOnlyBeatsProviderDefault()
    {
        WindowRuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        WindowQuery q;
        q.screenId = kScreen;
        q.virtualDesktop = 5;
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("screen"));
    }

    void testFallThroughToProviderDefault()
    {
        WindowRuleSet set;
        set.addRule(contextRule(QStringLiteral("default"), QString(), 0, QString(), QStringLiteral("default")));
        set.addRule(contextRule(QStringLiteral("screen"), kScreen, 0, QString(), QStringLiteral("screen")));
        RuleEvaluator eval(set);

        // A query on a screen with no specific rule falls through to the
        // empty-All{} provider default.
        WindowQuery q;
        q.screenId = QStringLiteral("HDMI-9");
        QCOMPARE(resolvedMode(eval, q), QStringLiteral("default"));
    }

    void testContextRuleMatchesWindowlessQuery()
    {
        // The cascade is resolved with a windowless context query — no
        // window attributes present. Context-only rules must still match.
        WindowRuleSet set;
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
        WindowRuleSet set;
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
        WindowRuleSet set;
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

        // Unknown screen -> provider default.
        WindowQuery def;
        def.screenId = QStringLiteral("VGA-3");
        QCOMPARE(resolvedMode(eval, def), QStringLiteral("default"));
    }
};

QTEST_GUILESS_MAIN(TestRuleEvaluatorCascade)
#include "test_ruleevaluator_cascade.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/ExclusionRules.h>
#include <PhosphorWindowRule/RuleEvaluator.h>

#include <QStringList>
#include <QTest>

using namespace PhosphorWindowRule;

namespace {

/// A per-window query mirroring how the effect builds one — `desktopFile`
/// from `KWin::Window::desktopFileName()`, `windowClass` from
/// `EffectWindow::windowClass()`.
WindowQuery windowQuery(const QString& desktopFile, const QString& windowClass)
{
    WindowQuery q;
    q.desktopFile = desktopFile;
    q.windowClass = windowClass;
    q.screenId = QStringLiteral("DP-1");
    return q;
}

} // namespace

class TestExclusionRules : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── Empty input ──

    void testEmptyLists_yieldEmptySet()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({}, {});
        QVERIFY(set.isEmpty());
    }

    void testAllEmptyPatternsDropped()
    {
        // Empty / blank patterns must not survive — a `Contains ""` predicate
        // would match every window.
        const WindowRuleSet set = ExclusionRules::toRuleSet({QString(), QStringLiteral("")}, {QString()});
        QVERIFY(set.isEmpty());
    }

    // ── Rule shape ──

    void testApplicationPattern_buildsDesktopFileExcludeRule()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({QStringLiteral("firefox")}, {});
        QCOMPARE(set.count(), 1);

        const WindowRule& rule = set.rules().first();
        QVERIFY(rule.enabled);
        QVERIFY(rule.hasTerminalAction());
        QVERIFY(rule.match.isLeaf());
        QCOMPARE(rule.match.predicate().field, Field::DesktopFile);
        QCOMPARE(rule.match.predicate().op, Operator::Contains);
        QCOMPARE(rule.match.predicate().value.toString(), QStringLiteral("firefox"));
        QCOMPARE(rule.actions.size(), 1);
        QCOMPARE(rule.actions.first().type, QString(ActionType::Exclude));
    }

    void testWindowClassPattern_buildsWindowClassExcludeRule()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({}, {QStringLiteral("steam")});
        QCOMPARE(set.count(), 1);
        QCOMPARE(set.rules().first().match.predicate().field, Field::WindowClass);
    }

    void testMixedLists_countMatchesSurvivingPatterns()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet(
            {QStringLiteral("firefox"), QString(), QStringLiteral("thunderbird")}, {QStringLiteral("steam")});
        // 2 app patterns + 1 class pattern survive; 1 empty dropped.
        QCOMPARE(set.count(), 3);
    }

    // ── Behaviour parity with matchesExclusionLists() ──

    void testEvaluator_excludesMatchingApplication()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({QStringLiteral("firefox")}, {});
        RuleEvaluator eval(set);
        const ResolvedActions r =
            eval.resolve(windowQuery(QStringLiteral("org.mozilla.firefox"), QStringLiteral("firefox")));
        QVERIFY(r.isExcluded());
    }

    void testEvaluator_excludesMatchingWindowClass()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({}, {QStringLiteral("Steam")});
        RuleEvaluator eval(set);
        const ResolvedActions r =
            eval.resolve(windowQuery(QStringLiteral("com.valvesoftware.Steam"), QStringLiteral("steam")));
        QVERIFY(r.isExcluded());
    }

    void testEvaluator_caseInsensitiveSubstring()
    {
        // matchesExclusionLists() is case-insensitive substring — parity check.
        const WindowRuleSet set = ExclusionRules::toRuleSet({QStringLiteral("FIRE")}, {});
        RuleEvaluator eval(set);
        QVERIFY(
            eval.resolve(windowQuery(QStringLiteral("org.mozilla.firefox"), QStringLiteral("firefox"))).isExcluded());
    }

    void testEvaluator_nonMatchingWindowNotExcluded()
    {
        const WindowRuleSet set = ExclusionRules::toRuleSet({QStringLiteral("firefox")}, {QStringLiteral("steam")});
        RuleEvaluator eval(set);
        const ResolvedActions r =
            eval.resolve(windowQuery(QStringLiteral("org.kde.konsole"), QStringLiteral("konsole")));
        QVERIFY(!r.isExcluded());
        QVERIFY(r.isEmpty());
    }

    void testEvaluator_absentWindowFieldDoesNotMatch()
    {
        // A windowless context query carries no desktopFile / windowClass —
        // an Exclude rule must not fire.
        const WindowRuleSet set = ExclusionRules::toRuleSet({QStringLiteral("firefox")}, {});
        RuleEvaluator eval(set);
        WindowQuery contextOnly;
        contextOnly.screenId = QStringLiteral("DP-1");
        QVERIFY(!eval.resolve(contextOnly).isExcluded());
    }
};

QTEST_MAIN(TestExclusionRules)
#include "test_exclusionrules.moc"

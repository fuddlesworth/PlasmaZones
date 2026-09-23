// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Evaluation-cost benchmark. As rule evaluation is O(rules) per query (vs the
// old cheap hash cascade), the match cache must cover the hot paths. This
// benchmark measures the uncached descending-priority walk, the cached path,
// and a representative composite-tree resolution.

#include "RuleTestHelpers.h"

#include <QTest>

using namespace PhosphorRules;
using namespace PhosphorRules::TestHelpers;

namespace {

/// A rule set of @p n window-property rules plus a context catch-all — a
/// realistic shape for the effect's animation app-rules path.
RuleSet buildRuleSet(int n)
{
    RuleSet set;
    for (int i = 0; i < n; ++i) {
        set.addRule(
            makeRule(QStringLiteral("rule-%1").arg(i), i,
                     MatchExpression::makeLeaf(Field::WindowClass, Operator::Contains, QStringLiteral("app-%1").arg(i)),
                     {floatAction()}));
    }
    set.addRule(makeRule(QStringLiteral("catch-all"), 0, MatchExpression{}, {engineMode(QStringLiteral("snapping"))}));
    return set;
}

WindowQuery sampleQuery()
{
    WindowQuery q;
    q.appId = QStringLiteral("org.kde.konsole");
    q.windowClass = QStringLiteral("app-37"); // matches one mid-list rule
    q.screenId = QStringLiteral("DP-2");
    q.virtualDesktop = 1;
    return q;
}

} // namespace

class TestRuleEvaluatorBenchmark : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void benchmarkResolveUncached_smallSet()
    {
        const RuleSet set = buildRuleSet(20);
        RuleEvaluator eval(set);
        const WindowQuery q = sampleQuery();
        QBENCHMARK {
            const ResolvedActions r = eval.resolve(q);
            Q_UNUSED(r);
        }
    }

    void benchmarkResolveUncached_largeSet()
    {
        const RuleSet set = buildRuleSet(200);
        RuleEvaluator eval(set);
        const WindowQuery q = sampleQuery();
        QBENCHMARK {
            const ResolvedActions r = eval.resolve(q);
            Q_UNUSED(r);
        }
    }

    void benchmarkResolveCached_largeSet()
    {
        const RuleSet set = buildRuleSet(200);
        RuleEvaluator eval(set);
        const WindowQuery q = sampleQuery();
        const QString winId = QStringLiteral("org.kde.konsole|abc");
        // Prime the cache.
        eval.resolveCached(winId, q);
        QBENCHMARK {
            const ResolvedActions r = eval.resolveCached(winId, q);
            Q_UNUSED(r);
        }
    }

    void benchmarkCompositeTreeEvaluation()
    {
        RuleSet set;
        for (int i = 0; i < 50; ++i) {
            set.addRule(makeRule(
                QStringLiteral("composite-%1").arg(i), i,
                MatchExpression::makeAll({
                    MatchExpression::makeLeaf(Field::AppId, Operator::AppIdMatches, QStringLiteral("app-%1").arg(i)),
                    MatchExpression::makeAny({
                        MatchExpression::makeLeaf(Field::WindowType, Operator::Equals, 2),
                        MatchExpression::makeLeaf(Field::Title, Operator::Regex,
                                                  QStringLiteral("Settings|Preferences")),
                    }),
                    MatchExpression::makeNone({
                        MatchExpression::makeLeaf(Field::ScreenId, Operator::Equals, QStringLiteral("eDP-1")),
                    }),
                }),
                {floatAction()}));
        }
        RuleEvaluator eval(set);

        WindowQuery q;
        q.appId = QStringLiteral("app-25");
        q.windowClass = QStringLiteral("app-25");
        q.title = QStringLiteral("App Settings");
        q.windowType = PhosphorProtocol::WindowType::Normal;
        q.screenId = QStringLiteral("DP-2");

        QBENCHMARK {
            const ResolvedActions r = eval.resolve(q);
            Q_UNUSED(r);
        }
    }

    void benchmarkResultSanity()
    {
        // Not a benchmark — a correctness gate so the benchmark above is
        // measuring a real resolution rather than an empty no-match.
        const RuleSet set = buildRuleSet(200);
        RuleEvaluator eval(set);
        const ResolvedActions r = eval.resolve(sampleQuery());
        QVERIFY(r.hasSlot(QString(ActionSlot::Float)));
        QVERIFY(r.hasSlot(QString(ActionSlot::EngineMode)));
    }
};

QTEST_GUILESS_MAIN(TestRuleEvaluatorBenchmark)
#include "test_ruleevaluator_benchmark.moc"

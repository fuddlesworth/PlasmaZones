// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// FuzzyMatcher — the fzf V2 port. What is pinned is the ORDERING fzf
// guarantees for unambiguous pairs (boundary beats mid-word, consecutive
// beats gapped, a doubled first-character bonus prefers a prefix), the
// exact score for a perfect match so perfectScore() stays honest, the
// case rules, the empty-input edges, and the positions used for
// highlighting.

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QtTest/QtTest>

using PhosphorShellLauncher::FuzzyMatch;
using PhosphorShellLauncher::FuzzyMatcher;

namespace {
int scoreOf(const char* pattern, const char* candidate, bool cs = false)
{
    const auto m = FuzzyMatcher::match(QString::fromUtf8(pattern), QString::fromUtf8(candidate), cs);
    return m ? m->score : -1;
}
} // namespace

class TestFuzzyMatcher : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyPatternMatchesEverythingAtZero();
    void emptyCandidateNeverMatchesAPattern();
    void everyPatternCharMustAppearInOrder();
    void caseInsensitiveByDefaultAndStrictWhenAsked();
    void prefixOutranksEmbeddedMatch();
    void consecutiveOutranksGapped();
    void wordBoundaryOutranksMidWord();
    void camelCaseTransitionCountsAsABoundary();
    void perfectMatchScoresExactlyPerfectScore();
    void positionsPointAtTheMatchedCharacters();
    void positionsFollowTheBestScoringAlignment();
};

void TestFuzzyMatcher::emptyPatternMatchesEverythingAtZero()
{
    const auto m = FuzzyMatcher::match(QString(), QStringLiteral("anything"));
    QVERIFY(m.has_value());
    QCOMPARE(m->score, 0);
    QVERIFY(m->positions.isEmpty());
}

void TestFuzzyMatcher::emptyCandidateNeverMatchesAPattern()
{
    QVERIFY(!FuzzyMatcher::match(QStringLiteral("a"), QString()).has_value());
}

void TestFuzzyMatcher::everyPatternCharMustAppearInOrder()
{
    QVERIFY(FuzzyMatcher::match(QStringLiteral("frx"), QStringLiteral("firefox")).has_value());
    // Present but out of order.
    QVERIFY(!FuzzyMatcher::match(QStringLiteral("xrf"), QStringLiteral("firefox")).has_value());
    // Absent.
    QVERIFY(!FuzzyMatcher::match(QStringLiteral("fireq"), QStringLiteral("firefox")).has_value());
    // Longer than the candidate.
    QVERIFY(!FuzzyMatcher::match(QStringLiteral("firefoxx"), QStringLiteral("firefox")).has_value());
}

void TestFuzzyMatcher::caseInsensitiveByDefaultAndStrictWhenAsked()
{
    QVERIFY(FuzzyMatcher::match(QStringLiteral("FIRE"), QStringLiteral("firefox")).has_value());
    QVERIFY(!FuzzyMatcher::match(QStringLiteral("FIRE"), QStringLiteral("firefox"), true).has_value());
    QVERIFY(FuzzyMatcher::match(QStringLiteral("Fire"), QStringLiteral("Firefox"), true).has_value());
}

void TestFuzzyMatcher::prefixOutranksEmbeddedMatch()
{
    // "fire" starts Firefox at a whitespace boundary (doubled first-char
    // bonus, inherited by the run) and sits mid-word in Wildfire.
    QVERIFY(scoreOf("fire", "Firefox") > scoreOf("fire", "Wildfire"));
}

void TestFuzzyMatcher::consecutiveOutranksGapped()
{
    QVERIFY(scoreOf("abc", "abc") > scoreOf("abc", "a_b_c"));
    // Same boundary context (both buried after an 'x'), so only the run
    // versus the gaps differs. NOT "xabcx" vs "axbxc": fzf genuinely ranks
    // the latter higher (62 vs 56), because a doubled whitespace-boundary
    // bonus on the leading 'a' outweighs two single-character gaps against
    // a buried run. That is the ordering this port reproduces on purpose.
    QVERIFY(scoreOf("abc", "xabcx") > scoreOf("abc", "xaxbxc"));
}

void TestFuzzyMatcher::wordBoundaryOutranksMidWord()
{
    // Both f's in "foo fish" start words; in "offer" they are buried.
    QVERIFY(scoreOf("ff", "foo fish") > scoreOf("ff", "offer"));
    // A delimiter boundary too: "bar" after '/' beats "bar" inside "rebar".
    QVERIFY(scoreOf("bar", "foo/bar") > scoreOf("bar", "rebar"));
}

void TestFuzzyMatcher::camelCaseTransitionCountsAsABoundary()
{
    // The 'V' in "fooView" earns the camel bonus; the 'v' in "fooview" does
    // not. Case-insensitive so both candidates match: the bonus is decided
    // by the candidate's RAW character class, not the folded comparison
    // character, which is what makes camelCase count at all.
    QVERIFY(scoreOf("v", "fooView") > scoreOf("v", "fooview"));
}

void TestFuzzyMatcher::perfectMatchScoresExactlyPerfectScore()
{
    // A run from the very start: first char 16 + 10*2, then 16 + 10 each.
    QCOMPARE(scoreOf("abc", "abc"), FuzzyMatcher::perfectScore(3));
    QCOMPARE(scoreOf("a", "a"), FuzzyMatcher::perfectScore(1));
    // Nothing beats it: a longer candidate with the same prefix scores the
    // same, never more, so perfectScore is a true ceiling.
    QCOMPARE(scoreOf("abc", "abcdef"), FuzzyMatcher::perfectScore(3));
    QVERIFY(scoreOf("abc", "xabc") < FuzzyMatcher::perfectScore(3));
}

void TestFuzzyMatcher::positionsPointAtTheMatchedCharacters()
{
    const auto m = FuzzyMatcher::match(QStringLiteral("fx"), QStringLiteral("firefox"));
    QVERIFY(m.has_value());
    QCOMPARE(m->positions, (QList<int>{0, 6}));
}

void TestFuzzyMatcher::positionsFollowTheBestScoringAlignment()
{
    // Two possible 'f's for the second pattern char; the consecutive one
    // at index 4 scores higher than a gapped alignment, so it is chosen.
    const auto m = FuzzyMatcher::match(QStringLiteral("ffo"), QStringLiteral("fx fox"));
    QVERIFY(m.has_value());
    QCOMPARE(m->positions, (QList<int>{0, 3, 4}));
}

QTEST_GUILESS_MAIN(TestFuzzyMatcher)

#include "test_fuzzymatcher.moc"

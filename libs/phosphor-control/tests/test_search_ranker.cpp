// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>

#include "PhosphorControl/SearchEntry.h"
#include "PhosphorControl/SearchRanker.h"

using PhosphorControl::SearchEntry;
using PhosphorControl::SearchRanker;

namespace {

SearchEntry pageEntry(const QString& title, const QStringList& keywords = {}, const QString& subtitle = {})
{
    SearchEntry e;
    e.kind = SearchEntry::Kind::Page;
    e.pageId = title.toLower();
    e.title = title;
    e.keywords = keywords;
    e.subtitle = subtitle;
    return e;
}

} // namespace

class TestSearchRanker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scoreTierOrdering()
    {
        // exact > prefix > word-start > substring > subsequence, all on title.
        const int exact = SearchRanker::score(QStringLiteral("rendering"), pageEntry(QStringLiteral("Rendering")));
        const int prefix = SearchRanker::score(QStringLiteral("render"), pageEntry(QStringLiteral("Rendering")));
        const int wordStart =
            SearchRanker::score(QStringLiteral("back"), pageEntry(QStringLiteral("Rendering backend")));
        const int substring = SearchRanker::score(QStringLiteral(" deri"), pageEntry(QStringLiteral("Rendering")));
        const int subseq = SearchRanker::score(QStringLiteral("rndrng"), pageEntry(QStringLiteral("Rendering")));

        QVERIFY(exact > prefix);
        QVERIFY(prefix > wordStart);
        QVERIFY(wordStart > substring);
        QVERIFY(substring > subseq);
        QVERIFY(subseq >= 120); // documented subsequence floor
    }

    void caseInsensitive()
    {
        QVERIFY(SearchRanker::score(QStringLiteral("RENDERING"), pageEntry(QStringLiteral("rendering"))) == 1000);
    }

    void noMatchScoresZero()
    {
        QCOMPARE(SearchRanker::score(QStringLiteral("xyzzy"), pageEntry(QStringLiteral("Rendering"))), 0);
    }

    void emptyQueryScoresZero()
    {
        QCOMPARE(SearchRanker::score(QStringLiteral("   "), pageEntry(QStringLiteral("Rendering"))), 0);
    }

    void titleBeatsKeywordBeatsSubtitle()
    {
        const QString q = QStringLiteral("color");
        const int titleHit = SearchRanker::score(q, pageEntry(QStringLiteral("Color")));
        const int keywordHit =
            SearchRanker::score(q, pageEntry(QStringLiteral("Appearance"), {QStringLiteral("color")}));
        const int subtitleHit =
            SearchRanker::score(q, pageEntry(QStringLiteral("Appearance"), {}, QStringLiteral("Color settings")));

        QVERIFY(keywordHit > 0);
        QVERIFY(subtitleHit > 0);
        QVERIFY(titleHit > keywordHit);
        QVERIFY(keywordHit > subtitleHit);
    }

    void keywordEnablesSynonymSearch()
    {
        // "opacity" should find a page titled "Appearance" via a keyword.
        SearchEntry e =
            pageEntry(QStringLiteral("Appearance"), {QStringLiteral("opacity"), QStringLiteral("transparency")});
        QVERIFY(SearchRanker::score(QStringLiteral("opacity"), e) > 0);
        QVERIFY(SearchRanker::score(QStringLiteral("transparency"), e) > 0);
    }

    void rankSortsByScoreAndFiltersAndCaps()
    {
        const QVector<SearchEntry> entries{
            pageEntry(QStringLiteral("Rendering")), // exact for "rendering"
            pageEntry(QStringLiteral("Rendering backend")), // prefix
            pageEntry(QStringLiteral("Window rendering hints")), // word-start
            pageEntry(QStringLiteral("Unrelated")), // no match → filtered
        };

        const QVector<SearchEntry> ranked = SearchRanker::rank(QStringLiteral("rendering"), entries);
        QCOMPARE(ranked.size(), 3);
        QCOMPARE(ranked.first().title, QStringLiteral("Rendering"));

        const QVector<SearchEntry> capped = SearchRanker::rank(QStringLiteral("rendering"), entries, 2);
        QCOMPARE(capped.size(), 2);
    }

    void rankTiesPreserveInputOrder()
    {
        // Two identical-scoring titles must come back in declaration order.
        const QVector<SearchEntry> entries{
            pageEntry(QStringLiteral("Snap A")),
            pageEntry(QStringLiteral("Snap B")),
        };
        const QVector<SearchEntry> ranked = SearchRanker::rank(QStringLiteral("snap"), entries);
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked.at(0).title, QStringLiteral("Snap A"));
        QCOMPARE(ranked.at(1).title, QStringLiteral("Snap B"));
    }

    void editDistanceBasics()
    {
        QCOMPARE(SearchRanker::editDistance(QStringLiteral("git"), QStringLiteral("git")), 0);
        QCOMPARE(SearchRanker::editDistance(QStringLiteral("gti"), QStringLiteral("git")),
                 2); // transposition = 2 edits
        QCOMPARE(SearchRanker::editDistance(QStringLiteral("kitten"), QStringLiteral("sitting")), 3);
        QCOMPARE(SearchRanker::editDistance(QString(), QStringLiteral("abc")), 3);
    }

    void closestTitleSuggestsOnTypoOnly()
    {
        const QVector<SearchEntry> entries{
            pageEntry(QStringLiteral("Rendering")),
            pageEntry(QStringLiteral("Animations")),
        };
        // One-edit typo → suggests the close title.
        QCOMPARE(SearchRanker::closestTitle(QStringLiteral("renderibg"), entries), QStringLiteral("Rendering"));
        // Garbage far from everything → no suggestion.
        QVERIFY(SearchRanker::closestTitle(QStringLiteral("zxqwvb"), entries).isEmpty());
    }

    void shortQueryDoesNotFuzzyMatch()
    {
        // A <4-char query must not subsequence-match — "gap" must not hit
        // "Graphics" (title or keyword), which was a real false positive.
        QCOMPARE(SearchRanker::score(QStringLiteral("gap"), pageEntry(QStringLiteral("Graphics"))), 0);
        QCOMPARE(SearchRanker::score(QStringLiteral("gap"),
                                     pageEntry(QStringLiteral("Rendering"), {QStringLiteral("graphics")})),
                 0);
        // 4+ char subsequence still matches.
        QVERIFY(SearchRanker::score(QStringLiteral("grph"), pageEntry(QStringLiteral("Graphics"))) > 0);
    }
};

QTEST_MAIN(TestSearchRanker)
#include "test_search_ranker.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QChar>
#include <QVector>

#include <algorithm>
#include <limits>

namespace PhosphorShellLauncher {

namespace {

// Sentinel for "no way to reach this cell". Half of INT_MIN so adding a
// gap penalty to it cannot wrap.
constexpr int Unreachable = std::numeric_limits<int>::min() / 2;

// fzf's character classes. The bonus a matched character earns depends on
// the class transition from its predecessor.
enum class CharClass {
    White,
    NonWord,
    Delimiter,
    Lower,
    Upper,
    Letter, // non-ASCII letter with no case distinction fzf cares about
    Number,
};

CharClass classOf(QChar c)
{
    if (c == u' ' || c == u'\t') {
        return CharClass::White;
    }
    if (c == u'/' || c == u',' || c == u':' || c == u';' || c == u'|') {
        return CharClass::Delimiter;
    }
    if (c.isDigit()) {
        return CharClass::Number;
    }
    if (c.isLetter()) {
        if (c.isLower()) {
            return CharClass::Lower;
        }
        if (c.isUpper()) {
            return CharClass::Upper;
        }
        return CharClass::Letter;
    }
    return CharClass::NonWord;
}

bool isWord(CharClass cls)
{
    return cls == CharClass::Lower || cls == CharClass::Upper || cls == CharClass::Letter || cls == CharClass::Number;
}

// fzf's bonusFor(prevClass, class), verbatim in effect: a word character
// right after whitespace / a delimiter / a non-word character earns the
// corresponding boundary bonus, a camelCase or letter-to-digit transition
// earns the camel bonus, and matched non-word characters earn their own.
int bonusFor(CharClass prev, CharClass cur)
{
    if (isWord(cur)) {
        switch (prev) {
        case CharClass::White:
            return FuzzyMatcher::BonusBoundaryWhite;
        case CharClass::Delimiter:
            return FuzzyMatcher::BonusBoundaryDelimiter;
        case CharClass::NonWord:
            return FuzzyMatcher::BonusBoundary;
        default:
            break;
        }
        if (prev == CharClass::Lower && cur == CharClass::Upper) {
            return FuzzyMatcher::BonusCamel123;
        }
        if (prev != CharClass::Number && cur == CharClass::Number) {
            return FuzzyMatcher::BonusCamel123;
        }
        return 0;
    }
    switch (cur) {
    case CharClass::White:
        return FuzzyMatcher::BonusBoundaryWhite;
    case CharClass::Delimiter:
        return FuzzyMatcher::BonusBoundaryDelimiter;
    case CharClass::NonWord:
        return FuzzyMatcher::BonusNonWord;
    default:
        return 0;
    }
}

} // namespace

int FuzzyMatcher::perfectScore(int length)
{
    if (length <= 0) {
        return 0;
    }
    // First character at a whitespace boundary with the doubled bonus,
    // every following character consecutive and inheriting that bonus.
    const int first = ScoreMatch + BonusBoundaryWhite * BonusFirstCharMultiplier;
    const int rest = ScoreMatch + BonusBoundaryWhite;
    return first + (length - 1) * rest;
}

std::optional<FuzzyMatch> FuzzyMatcher::match(QStringView pattern, QStringView candidate, bool caseSensitive)
{
    const int n = static_cast<int>(pattern.size());
    const int m = static_cast<int>(candidate.size());
    if (n == 0) {
        return FuzzyMatch{0, {}};
    }
    if (m == 0 || n > m) {
        return std::nullopt;
    }

    // Fold case once up front. Comparing folded copies is cheaper than
    // folding inside the O(n*m) loop, and keeps the equality test trivial.
    const auto fold = [caseSensitive](QChar c) {
        return caseSensitive ? c : c.toLower();
    };
    QVector<QChar> p(n);
    for (int i = 0; i < n; ++i) {
        p[i] = fold(pattern[i]);
    }
    QVector<QChar> t(m);
    QVector<int> bonus(m);
    CharClass prev = CharClass::White;
    for (int j = 0; j < m; ++j) {
        const QChar raw = candidate[j];
        t[j] = fold(raw);
        const CharClass cur = classOf(raw);
        bonus[j] = bonusFor(prev, cur);
        prev = cur;
    }

    // Early out: every pattern character must occur, in order. Cheaper
    // than the DP and it is the common case for a launcher (most
    // candidates do not match at all).
    {
        int pi = 0;
        for (int j = 0; j < m && pi < n; ++j) {
            if (t[j] == p[pi]) {
                ++pi;
            }
        }
        if (pi < n) {
            return std::nullopt;
        }
    }

    // DP over pattern rows. For row i and candidate column j:
    //   score[i][j]  best score with p[i] matched exactly at t[j]
    //   consec[i][j] length of the consecutive run ending there
    //   parent[i][j] the column p[i-1] was matched at, for backtracking
    QVector<int> score(n * m, Unreachable);
    QVector<int> consec(n * m, 0);
    QVector<int> parent(n * m, -1);
    const auto at = [m](int i, int j) {
        return i * m + j;
    };

    for (int j = 0; j < m; ++j) {
        if (t[j] == p[0]) {
            score[at(0, j)] = ScoreMatch + bonus[j] * BonusFirstCharMultiplier;
            consec[at(0, j)] = 1;
        }
    }

    for (int i = 1; i < n; ++i) {
        // Best gapped predecessor: max over k <= j-2 of
        //   score[i-1][k] + GapStart + GapExtension * (j - k - 2)
        // maintained incrementally so each cell is O(1).
        int gapped = Unreachable;
        int gappedFrom = -1;
        for (int j = 0; j < m; ++j) {
            if (j >= 2) {
                if (gapped != Unreachable) {
                    gapped += ScoreGapExtension;
                }
                const int fresh = score[at(i - 1, j - 2)];
                if (fresh != Unreachable && fresh + ScoreGapStart > gapped) {
                    gapped = fresh + ScoreGapStart;
                    gappedFrom = j - 2;
                }
            }
            if (t[j] != p[i]) {
                continue;
            }

            int best = Unreachable;
            int bestConsec = 0;
            int bestParent = -1;

            // Consecutive with the previous pattern character at j-1. A run
            // inherits the bonus of its first character (so "foo" after a
            // space earns the boundary bonus on every letter), unless this
            // character's own boundary bonus is higher, in which case fzf
            // starts a fresh run here.
            if (j >= 1 && score[at(i - 1, j - 1)] != Unreachable) {
                int run = consec[at(i - 1, j - 1)] + 1;
                int b = bonus[j];
                const int firstBonus = bonus[j - run + 1];
                if (b >= BonusBoundary && b > firstBonus) {
                    run = 1;
                } else {
                    b = std::max(b, std::max(BonusConsecutive, firstBonus));
                }
                best = score[at(i - 1, j - 1)] + ScoreMatch + b;
                bestConsec = run;
                bestParent = j - 1;
            }

            if (gapped != Unreachable) {
                const int candidateScore = gapped + ScoreMatch + bonus[j];
                if (candidateScore > best) {
                    best = candidateScore;
                    bestConsec = 1;
                    bestParent = gappedFrom;
                }
            }

            score[at(i, j)] = best;
            consec[at(i, j)] = bestConsec;
            parent[at(i, j)] = bestParent;
        }
    }

    int bestScore = Unreachable;
    int bestEnd = -1;
    for (int j = 0; j < m; ++j) {
        if (score[at(n - 1, j)] > bestScore) {
            bestScore = score[at(n - 1, j)];
            bestEnd = j;
        }
    }
    if (bestEnd < 0) {
        return std::nullopt;
    }

    FuzzyMatch result;
    result.score = bestScore;
    result.positions.resize(n);
    for (int i = n - 1, j = bestEnd; i >= 0; --i) {
        result.positions[i] = j;
        j = parent[at(i, j)];
    }
    return result;
}

} // namespace PhosphorShellLauncher

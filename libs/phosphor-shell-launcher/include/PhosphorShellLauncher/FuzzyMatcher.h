// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QStringView>

#include <optional>

namespace PhosphorShellLauncher {

// The score and matched positions of one pattern against one candidate.
struct PHOSPHORSHELLLAUNCHER_EXPORT FuzzyMatch
{
    // Higher is better. Comparable across candidates for the same pattern
    // and, because every provider scores through this one matcher, across
    // providers for the same query too.
    int score = 0;
    // Indices into the candidate of the characters that matched the
    // pattern, ascending. One per pattern character. For highlighting.
    QList<int> positions;
};

// fzf's default scorer ("FuzzyMatchV2"), ported so results rank the way a
// user who lives in fzf expects. Every pattern character must appear in
// the candidate in order; the score rewards matches at word boundaries
// (after whitespace, a delimiter, a non-word character, or at a camelCase
// / letter-to-digit transition), rewards consecutive runs, and penalises
// gaps. The first pattern character's boundary bonus is doubled, so
// "fire" prefers "Firefox" over "sunfire".
//
// Constants and the bonus table are fzf's own, so the ordering of any two
// candidates here matches fzf's ordering for the same input.
//
// Case-insensitive by default (fzf's smart-case is the caller's business:
// pass caseSensitive when the pattern contains an upper-case character).
class PHOSPHORSHELLLAUNCHER_EXPORT FuzzyMatcher
{
public:
    // Empty pattern matches everything with score 0 and no positions.
    // Note that no provider relies on this: each one decides for itself
    // whether an empty query lists anything (listsOnEmptyQuery) and skips
    // the matcher entirely in that case. The behaviour is kept because it
    // is the sane answer for a general-purpose matcher, not because a
    // caller depends on it. Empty candidate never matches a non-empty
    // pattern.
    [[nodiscard]] static std::optional<FuzzyMatch> match(QStringView pattern, QStringView candidate,
                                                         bool caseSensitive = false);

    /// fzf's smart case, as a decision a caller can make in one line.
    ///
    /// True when the pattern contains an upper-case character, which is the
    /// user saying they mean that capital. Providers pass the result of this
    /// to match(), so a lower-case query still matches anything and a typed
    /// capital narrows.
    [[nodiscard]] static bool patternIsCaseSensitive(QStringView pattern);

    // fzf's score constants, public so a provider that answers a query
    // exactly (the calculator) can pick a score that outranks any fuzzy
    // match without guessing at the scale.
    static constexpr int ScoreMatch = 16;
    static constexpr int ScoreGapStart = -3;
    static constexpr int ScoreGapExtension = -1;
    static constexpr int BonusBoundary = ScoreMatch / 2;
    static constexpr int BonusNonWord = ScoreMatch / 2;
    static constexpr int BonusCamel123 = BonusBoundary + ScoreGapExtension;
    static constexpr int BonusConsecutive = -(ScoreGapStart + ScoreGapExtension);
    static constexpr int BonusFirstCharMultiplier = 2;
    static constexpr int BonusBoundaryWhite = BonusBoundary + 2;
    static constexpr int BonusBoundaryDelimiter = BonusBoundary + 1;

    // The best score a pattern of `length` characters can reach: every
    // character matched, consecutively, at a whitespace boundary, first
    // character doubled. A provider returning an exact answer can add to
    // this to guarantee it sorts first.
    [[nodiscard]] static int perfectScore(int length);
};

} // namespace PhosphorShellLauncher

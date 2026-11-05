// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorControl/SearchEntry.h"
#include "phosphorcontrol_export.h"

#include <QString>
#include <QVector>

namespace PhosphorControl {

/**
 * @brief Pure, stateless scoring + ranking for the global settings search.
 *
 * Typo-tolerant ranking WITHOUT leading with edit distance (which surfaces
 * garbage on short queries). Per field, the best tier wins; fields are
 * weighted title > keywords > subtitle:
 *
 *   exact (1000) > prefix (800) > word-start prefix (600) > substring (400)
 *     > subsequence (120–330, denser = higher)
 *
 * A score of 0 means "no match" (filtered out). Edit distance is offered
 * separately as `closestTitle()` for a "did you mean …" fallback that callers
 * use ONLY when `rank()` returns nothing.
 *
 * Stateless + Qt-free beyond QString — fully unit-testable headless.
 */
class PHOSPHORCONTROL_EXPORT SearchRanker
{
public:
    /// Score `entry` against `query` (case-insensitive). 0 = no match.
    ///
    /// Retained as exported API and as the tested reference implementation of
    /// the tiering; it has no in-tree production caller (rank() is what the
    /// controller uses). Folds `query` on EVERY call, so it is a single-entry
    /// probe, not a loop body. `rank()` is the loop-safe entry point: it folds
    /// the query once and matches against each entry's pre-folded fields.
    /// Adopting this in a loop would reintroduce the per-entry folding cost
    /// that rank() avoids.
    static int score(const QString& query, const SearchEntry& entry);

    /// Return entries with score > 0, sorted by score desc (stable: input
    /// order preserved on ties). `limit < 0` = no cap.
    static QVector<SearchEntry> rank(const QString& query, const QVector<SearchEntry>& entries, int limit = -1);

    /// Classic Levenshtein edit distance over foldForSearch-normalised
    /// inputs, so it is case-, accent- and ß-insensitive.
    /// Retained as exported API; no in-tree production caller (closestTitle
    /// uses the pre-folded variant so it folds each side once).
    static int editDistance(const QString& a, const QString& b);

    /// Title of the entry whose title is the closest edit-distance match to
    /// `query`, for a "did you mean …" hint. Returns empty when nothing is
    /// within `maxDistance` (default scales with query length). Intended for
    /// the zero-results case only.
    static QString closestTitle(const QString& query, const QVector<SearchEntry>& entries, int maxDistance = -1);

    /** Fill @p entry's folded* fields from its display strings. Call once per
     *  entry when the index is built: rank() then matches against the folded
     *  copies instead of folding every field of every entry on every
     *  keystroke. Idempotent. */
    static void prefold(SearchEntry& entry);

    /** Normalise a string for search comparison: decompose, drop combining
     *  marks, then simple case-fold, with ß and ẞ mapped to "ss" explicitly
     *  afterwards. So "Café" matches "cafe" and "Größe" matches "grosse".
     *
     *  The explicit mapping is NOT redundant: Qt's toCaseFolded() is a SIMPLE
     *  fold, so it maps ẞ (U+1E9E) down to ß and never to "ss". Deleting the
     *  special case would silently break exactly the strings it names.
     *
     *  LOSSY BY DESIGN where a combining mark carries meaning: Vietnamese tone
     *  marks collapse, so má / mà / mả all fold to "ma" and such a query
     *  matches more titles than it names. Both sides fold identically, so that
     *  case only ever WIDENS the result set rather than hiding a match.
     *
     *  It does NOT fold characters with no canonical decomposition: ø, ł, đ
     *  and ı survive, so "Størrelse" is not reachable by typing "storrelse".
     *  That direction NARROWS, unlike the combining-mark case above. ß is
     *  special-cased because it is the one such character common enough in
     *  settings text to matter.
     *
     *  Returns an EMPTY string for input that is entirely combining marks.
     *  Callers matching with contains() must guard that case: contains("") is
     *  true, which would match everything.
     *
     *  Exposed rather than kept file-local because the sidebar rail
     *  (SidebarRows) matches its own breadcrumbs while this class matches the
     *  global index. Both must fold identically or the same query returns
     *  different results in the two surfaces, which reads as a bug in
     *  whichever one the user tried second. */
    static QString foldForSearch(const QString& s);

private:
    /// editDistance over inputs the caller has ALREADY folded. Exists so
    /// closestTitle folds each side once rather than once per comparison.
    static int editDistanceFolded(const QString& folded, const QString& otherFolded);
};

} // namespace PhosphorControl

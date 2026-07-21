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
 *     > subsequence (120–360, denser = higher)
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
    static int score(const QString& query, const SearchEntry& entry);

    /// Return entries with score > 0, sorted by score desc (stable: input
    /// order preserved on ties). `limit < 0` = no cap.
    static QVector<SearchEntry> rank(const QString& query, const QVector<SearchEntry>& entries, int limit = -1);

    /// Classic Levenshtein edit distance over foldForSearch-normalised
    /// inputs, so it is case-, accent- and ß-insensitive.
    static int editDistance(const QString& a, const QString& b);

    /// Title of the entry whose title is the closest edit-distance match to
    /// `query`, for a "did you mean …" hint. Returns empty when nothing is
    /// within `maxDistance` (default scales with query length). Intended for
    /// the zero-results case only.
    static QString closestTitle(const QString& query, const QVector<SearchEntry>& entries, int maxDistance = -1);

    /** Normalise a string for search comparison: decompose, drop combining
     *  marks, then full case-fold. So "Café" matches "cafe" and "Größe"
     *  matches "grosse".
     *
     *  LOSSY BY DESIGN where a combining mark carries meaning: Vietnamese tone
     *  marks collapse, so má / mà / mả all fold to "ma" and such a query
     *  matches more titles than it names. The error direction is deliberate —
     *  both sides fold identically, so this only ever WIDENS the result set
     *  rather than hiding a match, and the user still reads the real titles.
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
};

} // namespace PhosphorControl

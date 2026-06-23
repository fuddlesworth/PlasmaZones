// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/SearchRanker.h"

#include <QStringView>

#include <algorithm>

namespace PhosphorControl {

namespace {

// True if `n` is a prefix of a word inside `hay` (a "word" starts at index 0
// or after any non-alphanumeric character). Pure scan, no QRegularExpression.
bool wordStartPrefix(const QString& hay, const QString& n)
{
    const QStringView view(hay);
    for (int i = 0; i < hay.size(); ++i) {
        const bool isWordStart = (i == 0) || !hay.at(i - 1).isLetterOrNumber();
        if (isWordStart && view.mid(i).startsWith(n)) {
            return true;
        }
    }
    return false;
}

// Subsequence score: 0 if `n` is not an in-order subsequence of `hay`;
// otherwise 360 for a tight match down to 120 for a very gappy one. Only
// reached after exact/prefix/substring miss, so a match here always has gaps.
int subsequenceScore(const QString& hay, const QString& n)
{
    int hi = 0;
    int gaps = 0;
    // -1 so a match landing at index 0 isn't counted as a leading gap (a match
    // that first appears mid-string still does — that lead-in is a real gap). A
    // fully contiguous run can't reach here (caught earlier as a 400 substring),
    // so the densest reachable match has one gap, scoring 330.
    int prevMatch = -1;
    for (int ni = 0; ni < n.size(); ++ni) {
        while (hi < hay.size() && hay.at(hi) != n.at(ni)) {
            ++hi;
        }
        if (hi >= hay.size()) {
            return 0; // ran out of haystack — not a subsequence
        }
        if (hi != prevMatch + 1) {
            ++gaps;
        }
        prevMatch = hi;
        ++hi;
    }
    return std::max(120, 360 - std::min(gaps, 8) * 30);
}

// Best tier score for a single field against an already-lowercased needle.
int fieldScore(const QString& field, const QString& needle)
{
    if (field.isEmpty() || needle.isEmpty()) {
        return 0;
    }
    const QString f = field.toLower();
    if (f == needle) {
        return 1000;
    }
    if (f.startsWith(needle)) {
        return 800;
    }
    if (wordStartPrefix(f, needle)) {
        return 600;
    }
    if (f.contains(needle)) {
        return 400;
    }
    // Subsequence (gappy) matching is only trustworthy for longer queries — a
    // 3-char query subsequence-matches far too much ("gap" ⊂ "graphics"), which
    // surfaces nonsense. Short queries must hit exact/prefix/word-start/substring
    // or miss entirely.
    if (needle.size() < 4) {
        return 0;
    }
    return subsequenceScore(f, needle);
}

} // namespace

int SearchRanker::score(const QString& query, const SearchEntry& entry)
{
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) {
        return 0;
    }

    // Field weights: title authoritative, keywords strong, subtitle (breadcrumb)
    // weakest so a stray breadcrumb hit never outranks a real title match.
    int best = fieldScore(entry.title, needle);
    for (const QString& kw : entry.keywords) {
        best = std::max(best, fieldScore(kw, needle) * 85 / 100);
    }
    best = std::max(best, fieldScore(entry.subtitle, needle) * 60 / 100);
    return best;
}

QVector<SearchEntry> SearchRanker::rank(const QString& query, const QVector<SearchEntry>& entries, int limit)
{
    // Score once, keep the original index so ties resolve to input order
    // (std::sort is not stable; sorting on index as a tiebreaker is).
    struct Scored
    {
        int score;
        int index;
    };
    QVector<Scored> scored;
    scored.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const int s = SearchRanker::score(query, entries.at(i));
        if (s > 0) {
            scored.push_back({s, i});
        }
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.index < b.index;
    });

    QVector<SearchEntry> out;
    const int count = (limit < 0) ? scored.size() : std::min<int>(limit, scored.size());
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        out.push_back(entries.at(scored.at(i).index));
    }
    return out;
}

int SearchRanker::editDistance(const QString& a, const QString& b)
{
    const QString s = a.toLower();
    const QString t = b.toLower();
    const int n = s.size();
    const int m = t.size();
    if (n == 0) {
        return m;
    }
    if (m == 0) {
        return n;
    }

    // Two-row Wagner–Fischer.
    QVector<int> prev(m + 1);
    QVector<int> curr(m + 1);
    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }
    for (int i = 1; i <= n; ++i) {
        curr[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (s.at(i - 1) == t.at(j - 1)) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(curr);
    }
    return prev[m];
}

QString SearchRanker::closestTitle(const QString& query, const QVector<SearchEntry>& entries, int maxDistance)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return QString();
    }

    // Default tolerance scales with query length: ~1 edit per 4 chars, min 1,
    // capped at 3 so long garbage queries don't match arbitrary titles.
    const int tolerance = (maxDistance >= 0) ? maxDistance : std::clamp(static_cast<int>(needle.size()) / 4, 1, 3);

    QString bestTitle;
    int bestDistance = tolerance + 1;
    for (const SearchEntry& entry : entries) {
        if (entry.title.isEmpty()) {
            continue;
        }
        const int d = editDistance(needle, entry.title);
        if (d < bestDistance) {
            bestDistance = d;
            bestTitle = entry.title;
        }
    }
    return (bestDistance <= tolerance) ? bestTitle : QString();
}

} // namespace PhosphorControl

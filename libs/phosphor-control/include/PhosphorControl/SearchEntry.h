// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1Char>
#include <QString>
#include <QStringList>

namespace PhosphorControl {

/**
 * @brief One searchable item in the global settings search index.
 *
 * Pack-agnostic: a settings app populates these from its page registry
 * (kind Page), its authored anchor catalog (kind Section / Setting), and its
 * dynamic content providers (kind Entity). The ranker scores a query against
 * `title` / `keywords` / `subtitle`; activating a result navigates to
 * `address()` ("pageId" or "pageId#anchor") via SettingsController::navigateTo.
 *
 * All user-facing strings (title / subtitle / keywords) must be translated by
 * the producer — the index stores them verbatim.
 */
struct SearchEntry
{
    enum class Kind {
        Page, ///< A navigable settings page.
        Section, ///< A card / section within a page (anchor).
        Setting, ///< An individual setting row within a page (anchor).
        Entity, ///< A dynamic content item (rule, shader, layout, …).
        /// An app-level command rather than a destination (open an overlay,
        /// start a wizard). Carries `actionId` instead of a page address;
        /// the app's search UI dispatches it (GlobalSearchField emits
        /// actionTriggered) instead of navigating. Appended last so the
        /// integer values QML sees for the existing kinds are stable.
        Action
    };

    Kind kind = Kind::Page;
    /// Navigation target page id (must be a registered page). Empty for
    /// Kind::Action entries.
    QString pageId;
    /// App-defined command id for Kind::Action entries; empty otherwise.
    /// The producer and the app's action dispatcher share this vocabulary —
    /// the library never interprets it.
    QString actionId;
    /// Optional reveal anchor within the page. Empty for a plain page target.
    QString anchor;
    /// Primary display label (translated).
    QString title;
    /// Secondary context line — typically the breadcrumb (translated).
    QString subtitle;
    /// Icon name (freedesktop / Kirigami theme).
    QString icon;
    /// Extra synonyms / search terms (translated). Not displayed.
    QStringList keywords;
    /// True when the target row/card is only rendered in advanced mode, on a
    /// page that itself shows in BOTH modes. The registry's page-level tier
    /// cannot express this: the host page is visible, so without this flag a
    /// simple-mode search offers a result that reveals nothing. Producers set
    /// it to mirror the row's own advanced-only declaration.
    bool advancedOnly = false;

    /// Search-folded copies of the matched fields, filled by
    /// SearchRanker::prefold. EMPTY means "not prefolded" and the ranker folds
    /// on the fly, so an entry built without prefolding still matches
    /// correctly, just slower.
    ///
    /// These exist because folding is the ranker's dominant cost: matching one
    /// entry folds its title, its subtitle and every keyword, so a keystroke
    /// over a ~350-entry catalogue averaging ~4 keywords ran ~2000 Unicode NFD
    /// walks. The index is already cached behind a dirty flag, so that work
    /// was being repeated on immutable data.
    QString foldedTitle;
    QString foldedSubtitle;
    QStringList foldedKeywords;

    /// The navigable address consumed by SettingsController::navigateTo, which
    /// splits on the FIRST '#'. Page ids must therefore not contain '#' (they
    /// are registry identifiers, which never do); the anchor may.
    QString address() const
    {
        return anchor.isEmpty() ? pageId : (pageId + QLatin1Char('#') + anchor);
    }
};

} // namespace PhosphorControl

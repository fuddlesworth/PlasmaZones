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
        Entity ///< A dynamic content item (rule, shader, layout, …).
    };

    Kind kind = Kind::Page;
    /// Navigation target page id (must be a registered page).
    QString pageId;
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

    /// The navigable address consumed by SettingsController::navigateTo, which
    /// splits on the FIRST '#'. Page ids must therefore not contain '#' (they
    /// are registry identifiers, which never do); the anchor may.
    QString address() const
    {
        return anchor.isEmpty() ? pageId : (pageId + QLatin1Char('#') + anchor);
    }
};

} // namespace PhosphorControl

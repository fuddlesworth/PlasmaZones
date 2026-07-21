// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorControl/SearchEntry.h"
#include "phosphorcontrol_export.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace PhosphorControl {

class ApplicationController;
class ISearchProvider;

/**
 * @brief Global settings search: builds a ranked result list for a query.
 *
 * Generic shell component — any settings app on PhosphorControl gets global
 * search by wiring its content in:
 *   - PAGE entries are derived automatically from the ApplicationController's
 *     PageRegistry (navigable leaves; breadcrumb from the parent chain).
 *   - per-page synonyms via setPageKeywords(); section/setting anchors via
 *     addEntry(); dynamic content (rules/shaders/…) via registerProvider().
 *
 * QML binds `query` (the search field) and `results` (a ListView). Each result
 * map carries `title`, `subtitle`, `icon`, `kind`, `pageId`, `anchor`,
 * `address` and `actionId` — selecting one calls
 * `SettingsController::navigateTo(address)`, except for Kind::Action results,
 * which are dispatched by `actionId` instead.
 * `suggestion` holds a "did you mean …" title, populated only when a non-empty
 * query yields zero results.
 *
 * The index is built lazily and cached; invalidate() marks it stale (call when
 * dynamic content changes) so the next query rebuilds it — no per-entry live
 * signal wiring.
 */
class PHOSPHORCONTROL_EXPORT SearchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged)
    Q_PROPERTY(QString suggestion READ suggestion NOTIFY resultsChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    explicit SearchController(ApplicationController* app, QObject* parent = nullptr);
    ~SearchController() override;

    QString query() const;
    void setQuery(const QString& q);

    QVariantList results() const;
    int resultCount() const;
    QString suggestion() const;

    int limit() const;
    /// Max results. `n < 0` = uncapped; `n == 0` = no results.
    void setLimit(int n);

    /// Sets the synonyms for the auto-derived page entry for `pageId` (the ranker
    /// weighs them alongside the page's title/breadcrumb). Replaces any list
    /// previously set for this `pageId` — successive calls do not accumulate.
    void setPageKeywords(const QString& pageId, const QStringList& keywords);
    /// A static section / setting anchor entry (or any extra entry).
    void addEntry(const SearchEntry& entry);
    /// Register a dynamic content source (not owned; must outlive this).
    void registerProvider(ISearchProvider* provider);

    /// Mark the index stale; the next query (or the current one) rebuilds it.
    Q_INVOKABLE void invalidate();

Q_SIGNALS:
    void queryChanged();
    void resultsChanged();
    void limitChanged();

private:
    void recompute();
    QVector<SearchEntry> buildIndex();

    QPointer<ApplicationController> m_app = nullptr;
    QString m_query;
    int m_limit = 24;

    QHash<QString, QStringList> m_pageKeywords;
    QVector<SearchEntry> m_staticEntries;
    QVector<ISearchProvider*> m_providers;

    /// Entries already reported as living on an unregistered / non-navigable
    /// page, keyed by (pageId, title), so the drop warning is emitted once per
    /// offending ENTRY rather than once per index rebuild. Keyed per entry
    /// rather than per pageId because one bad id is typically shared by a whole
    /// card's worth of anchors, and naming only the first leaves the rest
    /// silent once it is fixed.
    QSet<QString> m_warnedEntryKeys;

    bool m_indexDirty = true;
    QVector<SearchEntry> m_index;
    QVariantList m_resultsVariant;
    QString m_suggestion;
};

} // namespace PhosphorControl

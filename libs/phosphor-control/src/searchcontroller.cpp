// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/SearchController.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/SearchRanker.h"

namespace PhosphorControl {

namespace {

constexpr QLatin1String kBreadcrumbSeparator(" › ");

QVariantMap toVariant(const SearchEntry& e)
{
    QVariantMap m;
    m.insert(QStringLiteral("title"), e.title);
    m.insert(QStringLiteral("subtitle"), e.subtitle);
    m.insert(QStringLiteral("icon"), e.icon);
    m.insert(QStringLiteral("kind"), static_cast<int>(e.kind));
    m.insert(QStringLiteral("pageId"), e.pageId);
    m.insert(QStringLiteral("anchor"), e.anchor);
    m.insert(QStringLiteral("address"), e.address());
    return m;
}

} // namespace

SearchController::SearchController(ApplicationController* app, QObject* parent)
    : QObject(parent)
    , m_app(app)
{
}

SearchController::~SearchController() = default;

QString SearchController::query() const
{
    return m_query;
}

void SearchController::setQuery(const QString& q)
{
    if (m_query == q) {
        return;
    }
    m_query = q;
    Q_EMIT queryChanged();
    recompute();
}

QVariantList SearchController::results() const
{
    return m_resultsVariant;
}

int SearchController::resultCount() const
{
    return m_resultsVariant.size();
}

QString SearchController::suggestion() const
{
    return m_suggestion;
}

int SearchController::limit() const
{
    return m_limit;
}

void SearchController::setLimit(int n)
{
    if (m_limit == n) {
        return;
    }
    m_limit = n;
    Q_EMIT limitChanged();
    recompute();
}

void SearchController::setPageKeywords(const QString& pageId, const QStringList& keywords)
{
    m_pageKeywords.insert(pageId, keywords);
    m_indexDirty = true;
}

void SearchController::addEntry(const SearchEntry& entry)
{
    m_staticEntries.push_back(entry);
    m_indexDirty = true;
}

void SearchController::registerProvider(ISearchProvider* provider)
{
    if (provider && !m_providers.contains(provider)) {
        m_providers.push_back(provider);
        m_indexDirty = true;
    }
}

void SearchController::invalidate()
{
    m_indexDirty = true;
    // Refresh live results if a query is active; otherwise the rebuild happens
    // lazily on the next setQuery.
    if (!m_query.trimmed().isEmpty()) {
        recompute();
    }
}

QVector<SearchEntry> SearchController::buildIndex() const
{
    QVector<SearchEntry> entries;
    if (m_app == nullptr || m_app->registry() == nullptr) {
        return entries + m_staticEntries;
    }

    const QList<PageRegistry::Entry> pages = m_app->registry()->allPages();

    // id → title map for breadcrumb assembly (parentChainFor yields ids).
    QHash<QString, QString> titles;
    titles.reserve(pages.size());
    for (const PageRegistry::Entry& e : pages) {
        titles.insert(e.id, e.title);
    }

    for (const PageRegistry::Entry& e : pages) {
        // Only navigable leaves are search targets; category / drill parents
        // carry no QML and aren't a destination.
        if (e.qmlSource.isEmpty()) {
            continue;
        }

        SearchEntry se;
        se.kind = SearchEntry::Kind::Page;
        se.pageId = e.id;
        se.title = e.title;
        se.icon = e.iconSource;
        se.keywords = m_pageKeywords.value(e.id);

        QStringList crumbs;
        const QStringList chain = m_app->parentChainFor(e.id);
        for (const QString& ancestorId : chain) {
            const QString t = titles.value(ancestorId);
            if (!t.isEmpty()) {
                crumbs << t;
            }
        }
        se.subtitle = crumbs.join(kBreadcrumbSeparator);

        entries.push_back(se);
    }

    entries += m_staticEntries;

    for (const ISearchProvider* provider : m_providers) {
        if (provider != nullptr) {
            entries += provider->searchEntries();
        }
    }

    return entries;
}

void SearchController::recompute()
{
    if (m_indexDirty) {
        m_index = buildIndex();
        m_indexDirty = false;
    }

    const QVector<SearchEntry> ranked = SearchRanker::rank(m_query, m_index, m_limit);

    m_resultsVariant.clear();
    m_resultsVariant.reserve(ranked.size());
    for (const SearchEntry& e : ranked) {
        m_resultsVariant.push_back(toVariant(e));
    }

    // "Did you mean …" only when a non-empty query found nothing.
    m_suggestion =
        (ranked.isEmpty() && !m_query.trimmed().isEmpty()) ? SearchRanker::closestTitle(m_query, m_index) : QString();

    Q_EMIT resultsChanged();
}

} // namespace PhosphorControl

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/SearchController.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/SearchRanker.h"

#include <QDebug>

namespace PhosphorControl {

namespace {

// U+203A (›) — QStringLiteral, not QLatin1String: the separator is multibyte
// UTF-8, which QLatin1String would reinterpret byte-per-char into mojibake.
QString breadcrumbSeparator()
{
    return QStringLiteral(" › ");
}

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
    m.insert(QStringLiteral("actionId"), e.actionId);
    return m;
}

} // namespace

SearchController::SearchController(ApplicationController* app, QObject* parent)
    : QObject(parent)
    , m_app(app)
{
    // The index is tier-filtered at build time (see buildIndex), so a
    // simple/advanced flip changes which entries belong in it. Without this
    // the filter would be computed once and the index would stay stale for
    // the rest of the session — search would keep returning the previous
    // mode's set. Wired here rather than in each app because the filter
    // itself lives in this library.
    if (m_app != nullptr && m_app->registry() != nullptr) {
        // visibleSetChanged rather than showAdvancedChanged: it covers both a
        // mode flip and a per-entry setPageVisibility restamp, which are the
        // two things that change what the tier filter admits.
        connect(m_app->registry(), &PageRegistry::visibleSetChanged, this, &SearchController::invalidate);
        // The index also derives its Page entries from the catalogue, which
        // GROWS at runtime (post-startup registerPage is a supported path),
        // so a late-registered page must reach the cache too.
        connect(m_app->registry(), &PageRegistry::pageRegistered, this, &SearchController::invalidate);
    }
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

// The three content mutators below route through invalidate() rather than
// setting m_indexDirty themselves: content added while a query is live (the
// normal case for registerProvider on async provider warm-up) must refresh the
// visible list, not sit stale until the next keystroke.

void SearchController::setPageKeywords(const QString& pageId, const QStringList& keywords)
{
    m_pageKeywords.insert(pageId, keywords);
    invalidate();
}

void SearchController::addEntry(const SearchEntry& entry)
{
    m_staticEntries.push_back(entry);
    invalidate();
}

void SearchController::registerProvider(ISearchProvider* provider)
{
    if (provider && !m_providers.contains(provider)) {
        m_providers.push_back(provider);
        invalidate();
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
        // Defensive fallback (unreachable in practice — the app is alive for the
        // controller's whole lifetime). Return NOTHING rather than the static
        // entries: without the registry there is no tier filter, so the
        // condensed simple pages' rows and their advanced twins would ALL
        // surface at once, which is exactly the duplication the filter below
        // exists to prevent. An empty result list is the honest answer when the
        // registry that defines reachability is gone.
        return entries;
    }

    const QList<PageRegistry::Entry> pages = m_app->registry()->allPages();

    // id → title map for breadcrumb assembly (parentChainFor yields ids).
    QHash<QString, QString> titles;
    titles.reserve(pages.size());
    for (const PageRegistry::Entry& e : pages) {
        titles.insert(e.id, e.title);
    }

    // Breadcrumb for a page: its ancestor titles, optionally with the page's own
    // title appended. Setting/section/entity entries live ON a page, so they
    // include the page itself ("Placement › Tiling › Appearance"); page entries
    // show ancestors only ("Placement › Tiling › Window").
    const auto breadcrumbFor = [&](const QString& pageId, bool includeSelf) -> QString {
        QStringList crumbs;
        const QStringList chain = m_app->parentChainFor(pageId);
        for (const QString& ancestorId : chain) {
            const QString t = titles.value(ancestorId);
            if (!t.isEmpty()) {
                crumbs << t;
            }
        }
        if (includeSelf) {
            const QString t = titles.value(pageId);
            if (!t.isEmpty()) {
                crumbs << t;
            }
        }
        return crumbs.join(breadcrumbSeparator());
    };

    for (const PageRegistry::Entry& e : pages) {
        // Only navigable leaves are search targets; category / drill parents
        // carry no QML and aren't a destination.
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        // Nor is a page the current simple/advanced tier hides: activating
        // such a result sends the app's mode gate somewhere else entirely,
        // so the user lands on an unrelated page with no reveal.
        if (!m_app->registry()->pageAllowedInCurrentMode(e.id)) {
            continue;
        }

        SearchEntry se;
        se.kind = SearchEntry::Kind::Page;
        se.pageId = e.id;
        se.title = e.title;
        se.icon = e.iconSource;
        se.keywords = m_pageKeywords.value(e.id);
        se.subtitle = breadcrumbFor(e.id, false);

        entries.push_back(se);
    }

    // Static + provider entries: auto-fill the breadcrumb from the page
    // hierarchy when the producer didn't set one, so section/setting/entity
    // results read consistently with page results. A producer-supplied subtitle
    // (e.g. a rule's match summary) is respected. Actions are excluded: they
    // are commands, not page-resident targets, so a page breadcrumb would
    // mislabel them (their pageId is empty anyway).
    // Same tier gate as the page loop above, shared by the static and
    // provider loops: a setting/section/entity entry whose host page the
    // current mode hides is not reachable, and the condensed simple pages
    // deliberately register rows that duplicate their advanced twins —
    // without this filter BOTH show up in either mode and one of them
    // navigates nowhere useful. Actions are dispatched by actionId rather
    // than by page address (and carry no pageId), so they are always kept.
    // An entry may also be advanced-only WITHIN a page both modes show (a row
    // or card the page hides in simple mode). The page-level check cannot see
    // that, so AND in the entry's own tier: otherwise simple mode offers a
    // result whose reveal target is collapsed to zero height.
    const auto tierAllows = [this](const SearchEntry& e) {
        if (e.kind == SearchEntry::Kind::Action || e.pageId.isEmpty()) {
            return true;
        }
        // Unregistered pageId — drop it, loudly. pageAllowedInCurrentMode
        // deliberately "expresses no opinion" on an unknown id (it is a tier
        // filter, not an existence check), so without this a typo'd pageId in
        // the catalogue sails through every gate below and produces a result
        // that navigates nowhere: the app's mode gate rejects the id and
        // bounces the user to a fallback page. The static allowlist that used
        // to bound this set is gone, and nothing replaced that check.
        // Registered is NOT enough: a CATEGORY id (empty qmlSource) is
        // registered but has no page body, so a result addressed at one
        // navigates into an empty viewport. The page loop above already
        // applies exactly this rule when it auto-derives entries; static and
        // provider entries were exempt from it, which is the same dead-result
        // class the registration check was added to close, one level down.
        if (!m_app->registry()->hasPage(e.pageId) || m_app->registry()->entry(e.pageId).qmlSource.isEmpty()) {
            qWarning() << "SearchController: dropping search entry" << e.title << "— its pageId" << e.pageId
                       << "is not a registered, navigable page";
            return false;
        }
        if (!m_app->registry()->pageAllowedInCurrentMode(e.pageId)) {
            return false;
        }
        return !e.advancedOnly || m_app->registry()->showAdvanced();
    };

    for (SearchEntry e : m_staticEntries) {
        if (!tierAllows(e)) {
            continue;
        }
        if (e.subtitle.isEmpty() && e.kind != SearchEntry::Kind::Page && e.kind != SearchEntry::Kind::Action) {
            e.subtitle = breadcrumbFor(e.pageId, true);
        }
        entries.push_back(e);
    }

    for (const ISearchProvider* provider : m_providers) {
        // No null check: registerProvider rejects nullptr and dedupes, and
        // there is no removal path, so an entry here is always non-null. A
        // check would read as protection against the dangling-provider case the
        // header warns about ("not owned; must outlive this"), which it cannot
        // detect — ISearchProvider is not a QObject, so there is nothing to
        // QPointer. Provider lifetime stays the caller's contract.
        const QVector<SearchEntry> provided = provider->searchEntries();
        for (SearchEntry e : provided) {
            if (!tierAllows(e)) {
                continue;
            }
            if (e.subtitle.isEmpty() && e.kind != SearchEntry::Kind::Page && e.kind != SearchEntry::Kind::Action) {
                e.subtitle = breadcrumbFor(e.pageId, true);
            }
            entries.push_back(e);
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

    QVariantList nextResults;
    nextResults.reserve(ranked.size());
    for (const SearchEntry& e : ranked) {
        nextResults.push_back(toVariant(e));
    }

    // "Did you mean …" only when a non-empty query found nothing. A limit of 0
    // empties results by the cap, not by absence of matches, so it must not
    // imply a typo.
    const QString nextSuggestion = (ranked.isEmpty() && m_limit != 0 && !m_query.trimmed().isEmpty())
        ? SearchRanker::closestTitle(m_query, m_index)
        : QString();

    // Emit only on a real change. recompute() runs on every invalidate(), and
    // visibleSetChanged fires it for mode flips that often do not alter what the
    // current query matches — re-emitting there would rebuild the QML ListView
    // and re-evaluate the results / resultCount / suggestion bindings for
    // nothing. Compares the built variant list, so a reordering counts as a
    // change while an identical rebuild does not.
    if (nextResults == m_resultsVariant && nextSuggestion == m_suggestion) {
        return;
    }
    m_resultsVariant = std::move(nextResults);
    m_suggestion = nextSuggestion;

    Q_EMIT resultsChanged();
}

} // namespace PhosphorControl

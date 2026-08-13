// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/SearchController.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/SearchRanker.h"

#include <QDebug>
#include <QLatin1Char>

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
    // 0 = both modes, 1 = simple-only, 2 = advanced-only. The UI badges
    // non-zero results and switches the mode before navigating to one the
    // live mode does not show.
    m.insert(QStringLiteral("mode"), static_cast<int>(e.mode));
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
    // The index tags every entry with its simple/advanced mode at build time
    // (see buildIndex), derived from the registry's page tiers. Without this
    // a setPageVisibility restamp would leave the cached tags stale for the
    // rest of the session. Wired here rather than in each app because the
    // classification itself lives in this library.
    if (m_app != nullptr && m_app->registry() != nullptr) {
        // visibleSetChanged covers both a mode flip and a per-entry
        // setPageVisibility restamp. Only the restamp changes the tags —
        // the index is mode-independent, so a plain flip rebuilds to an
        // identical list and recompute()'s results comparison suppresses
        // the emit.
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

QVector<SearchEntry> SearchController::buildIndex()
{
    QVector<SearchEntry> entries;
    if (m_app == nullptr || m_app->registry() == nullptr) {
        // Defensive fallback (unreachable in practice — the app is alive for the
        // controller's whole lifetime). Return NOTHING rather than the static
        // entries: without the registry there is no tier information, so every
        // entry's simple/advanced tag (and with it the UI's auto mode-switch)
        // would be a guess, and the unregistered-page drop below could not run
        // at all. An empty result list is the honest answer when the registry
        // that defines reachability is gone.
        return entries;
    }

    // By reference: both walks below only READ (fill a title hash, filter the
    // navigable leaves), and neither registers a page, so nothing can
    // reallocate m_pages while the reference is live. Copying the catalogue
    // here cost an Entry deep-copy per page on every index rebuild, which fires
    // on every mode flip and every provider warm-up.
    const QList<PageRegistry::Entry>& pages = m_app->registry()->allPagesRef();

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

    // An entry's mode tag from its reachability in each mode. The index is
    // deliberately NOT filtered by the live mode any more: a result the other
    // mode owns stays listed, badged with its mode, and the UI switches the
    // mode before navigating so the app's gate cannot redirect it away.
    const auto modeFor = [](bool inSimple, bool inAdvanced) {
        return (inSimple && inAdvanced) ? SearchEntry::Mode::Both
                                        : (inSimple ? SearchEntry::Mode::SimpleOnly : SearchEntry::Mode::AdvancedOnly);
    };

    for (const PageRegistry::Entry& e : pages) {
        // Only navigable leaves are search targets; category / drill parents
        // carry no QML and aren't a destination.
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        // Reachable in NEITHER mode (its own tier and an ancestor's tier
        // contradict, or the ancestry is unverifiable): no mode switch can
        // ever land on it, so it is not a destination.
        const bool inSimple = m_app->registry()->allowedInMode(e.id, false);
        const bool inAdvanced = m_app->registry()->allowedInMode(e.id, true);
        if (!inSimple && !inAdvanced) {
            continue;
        }

        SearchEntry se;
        se.kind = SearchEntry::Kind::Page;
        se.mode = modeFor(inSimple, inAdvanced);
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
    // Same mode classification as the page loop above, shared by the static
    // and provider loops: a setting/section/entity entry inherits its host
    // page's tiers, ANDed with its own `advancedOnly` flag — a row or card
    // the page renders only in advanced mode must classify AdvancedOnly even
    // though the page itself shows in both modes, or a simple-mode activation
    // would skip the mode switch and reveal a row collapsed to zero height.
    // Returns false to DROP the entry (unregistered / non-navigable page, or
    // reachable in neither mode); otherwise stamps e.mode and keeps it. The
    // condensed simple pages deliberately register rows that duplicate their
    // advanced twins; both surface, distinguished by their mode badge, and
    // either navigates correctly because the UI switches the mode first.
    // Actions are dispatched by actionId rather than by page address (and
    // carry no pageId), so they are always kept, tagged Both.
    const auto classifyMode = [this, &modeFor](SearchEntry& e) {
        if (e.kind == SearchEntry::Kind::Action || e.pageId.isEmpty()) {
            return true;
        }
        // Unregistered pageId — drop it, loudly. allowedInMode deliberately
        // "expresses no opinion" on an unknown id (it is a tier
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
        //
        // hasNavigablePage answers both halves in ONE index lookup with no
        // Entry copy: it is false for an unknown id and for a registered
        // category (empty qmlSource). A rebuild fires on every mode flip and
        // every provider warm-up while a query is live, so at catalogue scale
        // the copy this avoids is real.
        if (!m_app->registry()->hasNavigablePage(e.pageId)) {
            // Reported once per offending ENTRY, not once per pageId: one bad
            // id is typically shared by a whole card's worth of anchors, and
            // keying on the id alone names only the first of them, so a
            // maintainer fixes that row and the rest stay silent for the life
            // of the process. This is a static authoring defect in the
            // catalogue, not a runtime event, so re-warning on every rebuild
            // would bury genuinely new output behind one typo repeating on
            // each keystroke-adjacent invalidate. The DROP is still
            // re-evaluated every rebuild, so a page registered later rescues
            // its entries normally.
            // Join with the ASCII unit separator (0x1f): a printable-safe
            // delimiter that cannot occur in a kebab page id or a translated
            // title, so distinct (pageId, title) pairs never collide — unlike an
            // embedded NUL, which is a fragile idiom for a map key.
            const QString offender = e.pageId + QLatin1Char('\x1f') + e.title;
            if (!m_warnedEntryKeys.contains(offender)) {
                m_warnedEntryKeys.insert(offender);
                qWarning() << "SearchController: dropping search entry" << e.title << "— its pageId" << e.pageId
                           << "is not a registered, navigable page";
            }
            return false;
        }
        // An advancedOnly row can only be REACHED where its page shows in
        // advanced mode, so it zeroes the simple leg. An advancedOnly entry
        // on a SimpleOnly page is an authoring contradiction reachable in
        // neither mode — dropped like an unreachable page.
        const bool inSimple = m_app->registry()->allowedInMode(e.pageId, false) && !e.advancedOnly;
        const bool inAdvanced = m_app->registry()->allowedInMode(e.pageId, true);
        if (!inSimple && !inAdvanced) {
            return false;
        }
        e.mode = modeFor(inSimple, inAdvanced);
        return true;
    };

    for (SearchEntry e : m_staticEntries) {
        if (!classifyMode(e)) {
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
            if (!classifyMode(e)) {
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
        // Fold every entry's matched fields once here rather than once per
        // entry per keystroke inside the ranker.
        for (SearchEntry& e : m_index) {
            SearchRanker::prefold(e);
        }
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
    // visibleSetChanged fires it for mode flips, which never alter what the
    // current query matches (the index is mode-independent; only a restamp
    // changes the tags) — re-emitting there would rebuild the QML ListView
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

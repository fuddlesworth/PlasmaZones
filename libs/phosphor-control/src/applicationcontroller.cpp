// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/ApplicationController.h"

#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/StagingDomain.h"

#include <QDebug>
#include <QScopeGuard>

namespace PhosphorControl {

ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent)
    , m_registry(new PageRegistry(this))
{
}

ApplicationController::~ApplicationController() = default;

PageRegistry* ApplicationController::registry() const
{
    return m_registry;
}

bool ApplicationController::isDirty() const
{
    return m_dirty;
}

bool ApplicationController::isApplying() const
{
    return m_applying;
}

bool ApplicationController::isDiscarding() const
{
    return m_discarding;
}

QString ApplicationController::currentPageId() const
{
    return m_currentPageId;
}

void ApplicationController::setCurrentPageId(const QString& id)
{
    if (m_currentPageId == id) {
        return;
    }
    if (!id.isEmpty() && !m_registry->hasPage(id)) {
        qWarning() << "ApplicationController::setCurrentPageId: unknown page" << id;
        return;
    }
    // Record ordinary navigation in the back/forward history: push the
    // page being left and drop the forward trail (browser model). Moves
    // driven by goBack/goForward manage the stacks themselves and set
    // m_navigatingHistory to skip this. The startup transition (empty →
    // first page) records nothing — there is no page to go back to.
    const bool couldGoBack = canGoBack();
    const bool couldGoForward = canGoForward();
    if (!m_navigatingHistory) {
        if (!m_currentPageId.isEmpty()) {
            m_backHistory.append(m_currentPageId);
            if (m_backHistory.size() > kMaxHistoryEntries) {
                m_backHistory.removeFirst();
            }
        }
        m_forwardHistory.clear();
    }
    m_currentPageId = id;
    if (canGoBack() != couldGoBack || canGoForward() != couldGoForward) {
        Q_EMIT historyChanged();
    }
    // Discard a stale deep-link reveal anchor when navigation moves to a
    // different page than the one it targeted (discard-on-navigate-away).
    if (!m_pendingAnchor.isEmpty() && m_pendingAnchorPage != m_currentPageId) {
        m_pendingAnchor.clear();
        m_pendingAnchorPage.clear();
        Q_EMIT pendingAnchorChanged();
    }
    Q_EMIT currentPageIdChanged();
}

void ApplicationController::setPendingAnchor(const QString& pageId, const QString& anchor)
{
    if (pageId.isEmpty() || anchor.isEmpty()) {
        return;
    }
    m_pendingAnchorPage = pageId;
    m_pendingAnchor = anchor;
    Q_EMIT pendingAnchorChanged();
}

QString ApplicationController::takePendingAnchor(const QString& pageId)
{
    if (m_pendingAnchor.isEmpty() || m_pendingAnchorPage != pageId) {
        return QString();
    }
    const QString anchor = m_pendingAnchor;
    m_pendingAnchor.clear();
    m_pendingAnchorPage.clear();
    // No pendingAnchorChanged emit: the caller has the value and is acting on
    // it; re-emitting would only trigger further no-op takes.
    return anchor;
}

void ApplicationController::registerPage(PageController* page, const QString& parentId, const QString& title,
                                         const QUrl& qmlSource, const QString& iconSource, bool isCollapsible,
                                         bool hasDividerAfter, PageRegistry::PageVisibility visibility,
                                         const QString& counterpartId)
{
    if (!page) {
        qWarning() << "ApplicationController::registerPage: null page";
        return;
    }
    // Probe thread affinity BEFORE the registry accepts the entry and
    // BEFORE setParent. Cross-thread tracking would later be refused
    // by trackDomain, leaving the registry holding a page whose
    // parent + dirty-tracking are out of sync. Surface the failure
    // here so the caller sees a single rejection point.
    if (page->thread() != this->thread()) {
        qWarning() << "ApplicationController::registerPage: cross-thread page refused (controller in thread"
                   << this->thread() << ", page in thread" << page->thread() << ")";
        return;
    }

    PageRegistry::Entry entry;
    entry.id = page->id();
    entry.parentId = parentId;
    entry.title = title;
    entry.iconSource = iconSource;
    entry.qmlSource = qmlSource;
    entry.controller = page;
    entry.isCollapsible = isCollapsible;
    entry.hasDividerAfter = hasDividerAfter;
    entry.visibility = visibility;
    entry.counterpartId = counterpartId;
    // Reparent ONLY when registration is accepted — otherwise a
    // caller who built a null-parented page and held it on the
    // stack/heap would lose ownership to us on rejection (empty id,
    // duplicate, unknown parent) and we'd destroy the page when this
    // controller dies. Symmetric for registerDomain below.
    if (!m_registry->registerPage(std::move(entry))) {
        return;
    }
    if (!page->parent()) {
        page->setParent(this);
    }

    trackDomain(page);
}

void ApplicationController::registerDomain(StagingDomain* domain)
{
    if (!domain) {
        qWarning() << "ApplicationController::registerDomain: null domain";
        return;
    }
    // Probe thread affinity BEFORE setParent / trackDomain — same
    // rationale as registerPage above. trackDomain would refuse the
    // domain later anyway, but failing here keeps registration
    // atomic: either every side effect (reparent, m_domains insert,
    // dirty wiring) lands, or none does.
    if (domain->thread() != this->thread()) {
        qWarning() << "ApplicationController::registerDomain: cross-thread domain refused (controller in thread"
                   << this->thread() << ", domain in thread" << domain->thread() << ")";
        return;
    }
    // No PageRegistry-side validation gate for headless domains — but
    // trackDomain refuses duplicates with a warning. Only reparent if
    // tracking actually accepted the domain (i.e. it wasn't already
    // in m_domains). Pre-trackDomain check mirrors trackDomain's
    // duplicate guard exactly.
    const bool alreadyTracked = m_domains.contains(QPointer<StagingDomain>(domain));
    trackDomain(domain);
    if (!alreadyTracked && !domain->parent()) {
        domain->setParent(this);
    }
}

// NOTE: applyAll is the LEGACY sync slot. It drives each dirty domain's
// apply() inline and discards the applyResult signal — failures must be
// surfaced by the domain via its own `isDirty()` (kept true on failure)
// so the global dirty flag holds and the user can retry. Modern callers
// should prefer applyAllAsync(), which collects every applyResult into a
// single applyAllComplete(ok, errors) terminal signal. The sync entry
// point exists for QML callers that don't bind a Q_INVOKABLE result
// signal.
void ApplicationController::applyAll()
{
    // Refuse the sync entry point while the async batch is in flight
    // — mixing the two paths drives nested m_inTransaction toggles
    // and lets the trailing recomputeDirty observe mid-batch state.
    // Chrome always uses applyAllAsync; this guard pins the contract
    // for stray legacy callers.
    if (m_applying) {
        qWarning() << "ApplicationController::applyAll: refused — applyAllAsync already in flight";
        return;
    }
    if (m_discarding) {
        // Mutually exclusive with the discard batch: both paths flip
        // m_inTransaction and clobber each other's recomputeDirty
        // suppression window, and a domain told to apply AND discard
        // concurrently would land in an inconsistent state.
        qWarning() << "ApplicationController::applyAll: refused — discardAllAsync already in flight";
        return;
    }
    // Best-effort, no rollback: runs `apply()` on every dirty domain in
    // registration order. If domain N throws or fails internally,
    // domains 1..N-1 stay applied and N+1..M get their normal apply
    // call. StagingDomain::apply has a `void` return, so a domain that
    // wants to surface a partial-failure must do so via its own
    // signalling (see PlasmaZones::SettingsStagingDomain for an example
    // that keeps `isDirty()` true on failure so the global dirty flag
    // remains set and the user can retry).
    //
    // Snapshot m_domains before iterating: each domain->apply() fires
    // dirtyChanged synchronously, which routes through onDomainDirtyChanged
    // into recomputeDirty(), which may erase null entries from m_domains.
    // Iterating the live list would invalidate the outer iterators.
    //
    // m_inTransaction suppresses the inner per-domain recomputeDirty
    // walks (A15 followup) so the transaction is O(N) instead of O(N²).
    // The trailing recomputeDirty() with the flag cleared emits the
    // single net dirtyChanged for the whole batch.
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    m_inTransaction = true;
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty()) {
            domain->apply();
        }
    }
    m_inTransaction = false;
    recomputeDirty();
}

// NOTE: discardAll is the LEGACY sync slot — see applyAll above for the
// rationale. Discards each dirty domain's discardResult signal; callers
// that want a terminal completion signal should use discardAllAsync().
void ApplicationController::discardAll()
{
    if (m_discarding) {
        qWarning() << "ApplicationController::discardAll: refused — discardAllAsync already in flight";
        return;
    }
    if (m_applying) {
        // Symmetric to the cross-guard in applyAll — see comment there.
        qWarning() << "ApplicationController::discardAll: refused — applyAllAsync already in flight";
        return;
    }
    // Same best-effort semantics as applyAll() — see above.
    // Same iterator-invalidation rationale for the snapshot + the same
    // m_inTransaction batching to avoid O(N²) dirty recomputation.
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    m_inTransaction = true;
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty()) {
            domain->discard();
        }
    }
    m_inTransaction = false;
    recomputeDirty();
}

// NOTE: applyAllAsync / discardAllAsync / completeApplyIfDone /
// completeDiscardIfDone / forceResetAsyncState / asyncBatchTimeoutMs
// (READ/WRITE) live in applicationcontroller_async.cpp — same class,
// separate TU.

void ApplicationController::resetCurrentPage()
{
    // NOTE: this is a sync, best-effort entry point. A page whose
    // resetToDefaults() implementation does asynchronous work (e.g.
    // D-Bus round-trip to fetch factory values) will return before
    // the reset actually completes — the contract is "fire and
    // forget" with no terminal signal. A future resetCurrentPageAsync
    // (Q_INVOKABLE returning via a resetComplete signal) is reserved
    // for the first consumer that needs to observe completion;
    // current callers (chrome footer button) treat reset as best-effort
    // and rely on dirtyChanged to reflect the eventual outcome.
    if (m_currentPageId.isEmpty()) {
        return;
    }
    if (auto* page = m_registry->controller(m_currentPageId)) {
        page->resetToDefaults();
    }
}

namespace {
// Collects the registry's in-order list of navigable (qmlSource set)
// page ids and locates the current page's index inside it. Returns
// `currentIdx = -1` when the current page id is empty / not in the
// navigable set — the caller decides how to wrap.
struct NavigableState
{
    QStringList ids;
    int currentIdx = -1;
};

NavigableState collectNavigable(const PageRegistry* registry, const QString& currentPageId)
{
    NavigableState out;
    for (const auto& e : registry->allPages()) {
        if (e.qmlSource.isEmpty()) {
            continue;
        }
        // Keyboard next/prev must honour the same simple/advanced filter
        // as the sidebar — otherwise it walks onto pages the rail hides.
        // The current page is kept even if filtered (a transient state
        // during a mode flip) so the cursor position stays meaningful.
        if (!registry->pageAllowedInCurrentMode(e.id) && e.id != currentPageId) {
            continue;
        }
        if (e.id == currentPageId) {
            out.currentIdx = out.ids.size();
        }
        out.ids.append(e.id);
    }
    return out;
}
} // namespace

QString ApplicationController::gotoPreviousPage()
{
    const auto state = collectNavigable(m_registry, m_currentPageId);
    if (state.ids.isEmpty()) {
        return QString();
    }
    const int next = state.currentIdx <= 0 ? state.ids.size() - 1 : state.currentIdx - 1;
    setCurrentPageId(state.ids.at(next));
    return state.ids.at(next);
}

QString ApplicationController::gotoNextPage()
{
    const auto state = collectNavigable(m_registry, m_currentPageId);
    if (state.ids.isEmpty()) {
        return QString();
    }
    const int next = state.currentIdx < 0 || state.currentIdx == state.ids.size() - 1 ? 0 : state.currentIdx + 1;
    setCurrentPageId(state.ids.at(next));
    return state.ids.at(next);
}

bool ApplicationController::canGoBack() const
{
    return !m_backHistory.isEmpty();
}

bool ApplicationController::canGoForward() const
{
    return !m_forwardHistory.isEmpty();
}

QString ApplicationController::goBack()
{
    const bool couldGoBack = canGoBack();
    const bool couldGoForward = canGoForward();
    QString landed;
    while (!m_backHistory.isEmpty()) {
        const QString target = m_backHistory.takeLast();
        // Drop stale entries: the page was unregistered after being
        // visited, or the entry duplicates the current page (defensive —
        // setCurrentPageId's same-id early-return means recording never
        // produces one, but a subclass override could).
        if (target == m_currentPageId || !m_registry->hasPage(target)) {
            continue;
        }
        if (!m_currentPageId.isEmpty()) {
            m_forwardHistory.append(m_currentPageId);
        }
        // m_navigatingHistory suppresses the recording branch in
        // setCurrentPageId — a history move must not push onto the back
        // stack again or clear the forward trail it just extended.
        m_navigatingHistory = true;
        setCurrentPageId(target);
        m_navigatingHistory = false;
        landed = target;
        break;
    }
    if (canGoBack() != couldGoBack || canGoForward() != couldGoForward) {
        Q_EMIT historyChanged();
    }
    return landed;
}

QString ApplicationController::goForward()
{
    const bool couldGoBack = canGoBack();
    const bool couldGoForward = canGoForward();
    QString landed;
    while (!m_forwardHistory.isEmpty()) {
        const QString target = m_forwardHistory.takeLast();
        // Same stale-entry policy as goBack.
        if (target == m_currentPageId || !m_registry->hasPage(target)) {
            continue;
        }
        if (!m_currentPageId.isEmpty()) {
            m_backHistory.append(m_currentPageId);
        }
        m_navigatingHistory = true;
        setCurrentPageId(target);
        m_navigatingHistory = false;
        landed = target;
        break;
    }
    if (canGoBack() != couldGoBack || canGoForward() != couldGoForward) {
        Q_EMIT historyChanged();
    }
    return landed;
}

QStringList ApplicationController::parentChainFor(const QString& id) const
{
    // Nesting-depth cap. 32 hops is well above any realistic sidebar
    // nesting (typical settings apps cap at 3-4 levels). PageRegistry::
    // registerPage refuses an entry whose parentId isn't already
    // registered, which makes registry-internal cycles structurally
    // impossible — so this cap is purely a nesting-depth limit.
    constexpr int kMaxParentChainHops = 32;

    QStringList chain;
    QString cursor = id;
    // Walk parent links upward; cap at kMaxParentChainHops to bound depth.
    for (int i = 0; i < kMaxParentChainHops; ++i) {
        if (!m_registry->hasPage(cursor)) {
            // Unknown id is a legitimate query path (QML probing during
            // bootstrap before pages are registered). Return whatever
            // we've collected so far without warning — the warning below
            // is reserved for actual depth-exceeded cases.
            return chain;
        }
        const QString parent = m_registry->entry(cursor).parentId;
        if (parent.isEmpty()) {
            return chain;
        }
        chain.prepend(parent);
        cursor = parent;
    }
    // Reached the hop cap on a known starting id without hitting a root —
    // indicates the parent chain is nested deeper than the cap allows.
    // Warn so the bug surfaces instead of silently producing a truncated
    // chain.
    qWarning() << "ApplicationController::parentChainFor: page nested deeper than" << kMaxParentChainHops
               << "levels walking up from id" << id;
    return chain;
}

void ApplicationController::trackDomain(StagingDomain* domain)
{
    if (domain->thread() != this->thread()) {
        // Cross-thread registration would route dirtyChanged via
        // queued connection (slow + reorderable) AND fail at the
        // subsequent setParent call in registerPage / registerDomain
        // ("QObject::moveToThread: Cannot move objects with a
        // parent"). Refuse loudly so the caller surfaces the bug
        // instead of producing half-wired state.
        qWarning() << "ApplicationController::trackDomain: cross-thread domain refused (controller in thread"
                   << this->thread() << ", domain in thread" << domain->thread() << ")";
        return;
    }
    const QPointer<StagingDomain> tracked(domain);
    if (m_domains.contains(tracked)) {
        // Same domain registered twice (e.g. once via registerPage and
        // once via registerDomain). Connecting `dirtyChanged` a second
        // time would double-fire `recomputeDirty()` per emit — silently
        // wastes work and is always a caller bug.
        qWarning() << "ApplicationController::trackDomain: domain already tracked, ignoring";
        return;
    }
    m_domains.append(tracked);
    // Qt::UniqueConnection guards against the (already-checked-above) duplicate
    // registration path picking up a stale entry whose QPointer outlived its
    // pointee — defence-in-depth so double-tracking can never double-fire.
    connect(domain, &StagingDomain::dirtyChanged, this, &ApplicationController::onDomainDirtyChanged,
            Qt::UniqueConnection);
    // Run recomputeDirty unconditionally — even when the new domain
    // is clean, the walk compacts any null QPointer entries that
    // accumulated from previously-destroyed domains. Cost is O(N)
    // for a settings app's typical N (~20 domains), negligible.
    recomputeDirty();
}

void ApplicationController::onDomainDirtyChanged()
{
    // During applyAll / discardAll batches the outer transaction
    // emits a single recomputeDirty at the end (A15 followup) —
    // skip the per-edge walk so the batch is O(N) overall.
    if (m_inTransaction) {
        return;
    }
    recomputeDirty();
}

void ApplicationController::recomputeDirty()
{
    // Re-entrancy guard. `isDirty()` is a virtual exposed for
    // subclass override; a misbehaving override that emits
    // dirtyChanged from inside its getter would route through
    // onDomainDirtyChanged into recomputeDirty while the outer call
    // still holds iterators into m_domains — UB on QList erase.
    //
    // When the guard rejects a re-entrant call, record that a
    // cascaded recompute was requested so we can replay it on the
    // next event-loop turn. Without this, the outer walk would
    // observe pre-cascade `isDirty()` values and the cascaded dirty
    // edge would never be reflected in m_dirty (silently swallowed).
    if (m_recomputingDirty) {
        m_recomputeDirtyPending = true;
        return;
    }
    m_recomputingDirty = true;
    auto guard = qScopeGuard([this]() {
        m_recomputingDirty = false;
        // Replay a deferred recompute on the next event-loop turn
        // if any was requested while we were running. Queued so the
        // call stack unwinds first — direct dispatch here would
        // re-arm the guard recursively.
        if (m_recomputeDirtyPending) {
            m_recomputeDirtyPending = false;
            QMetaObject::invokeMethod(this, &ApplicationController::recomputeDirty, Qt::QueuedConnection);
        }
    });

    // Single pass: compact null QPointers and compute the dirty fold
    // together. A domain that was destroyed while tracked leaves a null
    // entry in m_domains; the contract is "domains outlive the controller"
    // but a poorly-ordered teardown shouldn't leak entries across the
    // lifetime of a long-running app.
    bool any = false;
    auto writeIt = m_domains.begin();
    for (auto readIt = m_domains.begin(); readIt != m_domains.end(); ++readIt) {
        if (!*readIt) {
            continue;
        }
        if ((*readIt)->isDirty()) {
            any = true;
        }
        if (writeIt != readIt) {
            *writeIt = *readIt;
        }
        ++writeIt;
    }
    if (writeIt != m_domains.end()) {
        m_domains.erase(writeIt, m_domains.end());
    }
    if (any == m_dirty) {
        return;
    }
    m_dirty = any;
    Q_EMIT dirtyChanged();
}

} // namespace PhosphorControl

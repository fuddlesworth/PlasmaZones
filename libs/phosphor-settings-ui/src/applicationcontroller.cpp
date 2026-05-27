// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/ApplicationController.h"

#include "PhosphorSettingsUi/PageController.h"
#include "PhosphorSettingsUi/PageRegistry.h"
#include "PhosphorSettingsUi/StagingDomain.h"

#include <QDebug>
#include <QScopeGuard>
#include <QTimer>

namespace PhosphorSettingsUi {

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
    m_currentPageId = id;
    Q_EMIT currentPageIdChanged();
}

void ApplicationController::registerPage(PageController* page, const QString& parentId, const QString& title,
                                         const QUrl& qmlSource, const QString& iconSource, bool isCollapsible,
                                         bool hasDividerAfter)
{
    if (!page) {
        qWarning() << "ApplicationController::registerPage: null page";
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

void ApplicationController::applyAllAsync()
{
    // Already in flight — second click is a no-op rather than starting
    // a parallel batch. UnsavedChangesFooter's Save button is gated on
    // !applying so a user shouldn't reach this in practice, but the
    // guard keeps the contract clean for QML callers that bypass the
    // chrome.
    if (m_applying || m_discarding) {
        // Same mutual-exclusion rationale as the sync paths — both
        // async batches share m_inTransaction and per-domain
        // outstanding sets; running them concurrently would corrupt
        // either book.
        return;
    }
    // Count dirty domains and snapshot the iteration target to survive
    // any synchronous m_domains mutation during apply (same rationale
    // as applyAll above).
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    QList<StagingDomain*> dirty;
    dirty.reserve(snapshot.size());
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty())
            dirty.append(domain.data());
    }
    if (dirty.isEmpty()) {
        // Nothing to do — drive applyingChanged through the full
        // false→true→false transition (matches the dirty-domain
        // path's observable contract) so consumers binding "show
        // toast on applyingChanged false→true" don't silently miss
        // the no-op batch.
        m_applying = true;
        Q_EMIT applyingChanged();
        m_applying = false;
        Q_EMIT applyingChanged();
        Q_EMIT applyAllComplete(true, QStringList{});
        return;
    }

    m_applyErrors.clear();
    m_applyOutstanding.clear();
    m_applyOutstanding.reserve(dirty.size());
    for (StagingDomain* d : dirty)
        m_applyOutstanding.insert(d);
    // Set pending counter from the outstanding set, not from the
    // input list — duplicate StagingDomain pointers in `dirty`
    // (defence-in-depth against trackDomain's contains-check) would
    // otherwise leave the counter > set size and the batch stuck
    // permanently mid-flight.
    m_applyPending = m_applyOutstanding.size();
    // Bump the generation BEFORE flipping m_applying / emitting
    // applyingChanged so any slot that reads m_applyGeneration from
    // the change handler sees the new value, not the previous batch's.
    ++m_applyGeneration;
    m_applying = true;
    // Set m_inTransaction BEFORE applyingChanged emits so any slot
    // that triggers domain dirtyChanged in response (cascade) takes
    // the m_inTransaction-guarded path inside onDomainDirtyChanged
    // instead of running a full O(N) recomputeDirty walk (the A15
    // batching optimisation).
    m_inTransaction = true;
    Q_EMIT applyingChanged();

    // Hard timeout — if any pending domain hasn't reported by the
    // deadline, synthesise a failure entry per pending domain, zero
    // the counter, and emit applyAllComplete(false, …). Without this
    // a stuck D-Bus reply with no client-side timeout would freeze
    // the chrome's "Saving…" indicator indefinitely.
    //
    // Captures the generation counter so a stale timer from a
    // previous batch can't mistakenly fire against a new batch the
    // user kicked off after the first one already completed.
    const quint64 generation = m_applyGeneration;
    QTimer::singleShot(m_asyncBatchTimeoutMs, this, [this, generation]() {
        if (generation != m_applyGeneration)
            return;
        if (!m_applying || m_applyPending == 0)
            return;
        // Append one error per stuck domain, naming each by its
        // objectName when set (consumers can stamp this in their
        // constructor) so the chrome's toast can list which page
        // wedged instead of N identical strings.
        for (StagingDomain* stuck : m_applyOutstanding) {
            const QString name = stuck ? stuck->objectName() : QString();
            m_applyErrors.append(
                name.isEmpty() ? QStringLiteral("Domain did not report apply completion within timeout")
                               : QStringLiteral("Domain %1 did not report apply completion within timeout").arg(name));
        }
        m_applyPending = 0;
        completeApplyIfDone();
    });

    // Per-domain lambdas share the same `generation` snapshot captured
    // above for the timer — see the generation-counter comment in the
    // header. Reusing the variable keeps the timer + per-domain
    // lambdas semantically equivalent for stale-fire detection.
    //
    // Two-pass: connect ALL per-domain terminal handlers BEFORE
    // calling any `apply()`. If domain N's synchronous apply() were
    // to cascade and trigger domain N+1's `applyResult` (e.g. a
    // shared signal bus, a cascading config-write), the single-pass
    // form would lose that emission because N+1's lambda hadn't been
    // connected yet — m_applyPending then never reaches 0 and the
    // 60 s timer is the only recovery. Two-pass closes that hole.
    for (StagingDomain* domain : dirty) {
        // Use Qt::SingleShotConnection so the lambda self-disconnects
        // after first fire. Avoids the prior heap-allocated
        // QMetaObject::Connection*  pattern that leaked when a domain
        // was destroyed mid-batch (Qt auto-disconnects on sender
        // destruction without running the manual cleanup body).
        //
        // The lambda checks generation FIRST (stale-batch guard) and
        // then removes the domain from m_applyOutstanding — if the
        // domain isn't in the set, this is the SECOND terminal fire
        // (applyResult ran sync then destroyed() fired, or vice-
        // versa) and we no-op so the pending counter only ticks once
        // per domain regardless of how many terminal signals arrive.
        connect(
            domain, &StagingDomain::applyResult, this,
            [this, domain, generation](bool ok, const QString& error) {
                if (generation != m_applyGeneration)
                    return;
                if (m_applyOutstanding.remove(domain) == 0)
                    return;
                if (!ok && !error.isEmpty())
                    m_applyErrors.append(error);
                else if (!ok)
                    m_applyErrors.append(QStringLiteral("Unknown error"));
                --m_applyPending;
                completeApplyIfDone();
            },
            Qt::SingleShotConnection);
        // Companion guard for the destroyed-mid-batch case: if the
        // domain QObject dies before it emits applyResult, this fires
        // and ticks the counter so m_applying doesn't stall true.
        // Shares the generation + outstanding-set protocol with the
        // applyResult lambda above so the two are idempotent.
        connect(
            domain, &QObject::destroyed, this,
            [this, domain, generation]() {
                if (generation != m_applyGeneration)
                    return;
                if (m_applyOutstanding.remove(domain) == 0)
                    return;
                m_applyErrors.append(QStringLiteral("Domain destroyed before apply completed"));
                --m_applyPending;
                completeApplyIfDone();
            },
            Qt::SingleShotConnection);
    }
    // Second pass — every lambda is wired, now drive the apply()s.
    for (StagingDomain* domain : dirty) {
        domain->apply();
    }
}

void ApplicationController::discardAllAsync()
{
    if (m_discarding || m_applying) {
        // Symmetric to applyAllAsync — see comment there.
        return;
    }
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    QList<StagingDomain*> dirty;
    dirty.reserve(snapshot.size());
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty())
            dirty.append(domain.data());
    }
    if (dirty.isEmpty()) {
        // Drive discardingChanged through the full false→true→false
        // transition (matches the dirty-domain path's contract).
        m_discarding = true;
        Q_EMIT discardingChanged();
        m_discarding = false;
        Q_EMIT discardingChanged();
        Q_EMIT discardAllComplete(true, QStringList{});
        return;
    }

    m_discardErrors.clear();
    m_discardOutstanding.clear();
    m_discardOutstanding.reserve(dirty.size());
    for (StagingDomain* d : dirty)
        m_discardOutstanding.insert(d);
    // See applyAllAsync rationale for the from-set sizing.
    m_discardPending = m_discardOutstanding.size();
    ++m_discardGeneration;
    m_discarding = true;
    m_inTransaction = true;
    Q_EMIT discardingChanged();

    // Same hard-timeout safety net as applyAllAsync — see comment
    // there for rationale. Generation capture protects against a
    // stale timer firing against a subsequent batch.
    const quint64 generation = m_discardGeneration;
    QTimer::singleShot(m_asyncBatchTimeoutMs, this, [this, generation]() {
        if (generation != m_discardGeneration)
            return;
        if (!m_discarding || m_discardPending == 0)
            return;
        // Same per-domain naming as the apply path so the chrome can
        // identify the wedged page(s).
        for (StagingDomain* stuck : m_discardOutstanding) {
            const QString name = stuck ? stuck->objectName() : QString();
            m_discardErrors.append(
                name.isEmpty()
                    ? QStringLiteral("Domain did not report discard completion within timeout")
                    : QStringLiteral("Domain %1 did not report discard completion within timeout").arg(name));
        }
        m_discardPending = 0;
        completeDiscardIfDone();
    });

    // Per-domain lambdas reuse the same `generation` snapshot above
    // (timer + apply/destroyed lambdas all key off it for stale-fire
    // detection). Two-pass connect-then-discard mirrors the
    // applyAllAsync structure so cross-domain sync cascades don't
    // lose terminal signals.
    for (StagingDomain* domain : dirty) {
        // Same generation + outstanding-set guard pattern as
        // applyAllAsync above. See the rationale comment there.
        connect(
            domain, &StagingDomain::discardResult, this,
            [this, domain, generation](bool ok, const QString& error) {
                if (generation != m_discardGeneration)
                    return;
                if (m_discardOutstanding.remove(domain) == 0)
                    return;
                if (!ok && !error.isEmpty())
                    m_discardErrors.append(error);
                else if (!ok)
                    m_discardErrors.append(QStringLiteral("Unknown error"));
                --m_discardPending;
                completeDiscardIfDone();
            },
            Qt::SingleShotConnection);
        connect(
            domain, &QObject::destroyed, this,
            [this, domain, generation]() {
                if (generation != m_discardGeneration)
                    return;
                if (m_discardOutstanding.remove(domain) == 0)
                    return;
                m_discardErrors.append(QStringLiteral("Domain destroyed before discard completed"));
                --m_discardPending;
                completeDiscardIfDone();
            },
            Qt::SingleShotConnection);
    }
    for (StagingDomain* domain : dirty) {
        domain->discard();
    }
}

void ApplicationController::completeApplyIfDone()
{
    if (m_applyPending > 0)
        return;
    m_inTransaction = false;
    m_applyOutstanding.clear();
    // State-change emission order:
    //   1) m_applying = false (set field first so probes see it)
    //   2) applyingChanged   (state-bit observers refresh)
    //   3) applyAllComplete  (terminal-batch signal fires while applying
    //                         is still observably false; a slot that
    //                         re-invokes applyAllAsync from here starts
    //                         a fresh batch cleanly)
    //   4) recomputeDirty    (dirtyChanged-driven side effects can now
    //                         observe both applying=false AND the
    //                         terminal signal has already been emitted —
    //                         no chance of Complete arriving AFTER a
    //                         re-invoked batch's applyingChanged(true))
    const bool ok = m_applyErrors.isEmpty();
    const QStringList errors = std::move(m_applyErrors);
    // Defensive: std::move leaves the source in a valid-but-
    // unspecified state. The next applyAllAsync also clears, but a
    // re-entrant applyAllComplete listener that reads m_applyErrors
    // would otherwise see whatever the QStringList's move-from
    // implementation left behind.
    m_applyErrors.clear();
    m_applying = false;
    Q_EMIT applyingChanged();
    Q_EMIT applyAllComplete(ok, errors);
    recomputeDirty();
}

void ApplicationController::completeDiscardIfDone()
{
    if (m_discardPending > 0)
        return;
    m_inTransaction = false;
    m_discardOutstanding.clear();
    // Same Complete-before-recomputeDirty ordering as completeApplyIfDone —
    // see the rationale comment there.
    const bool ok = m_discardErrors.isEmpty();
    const QStringList errors = std::move(m_discardErrors);
    // Defensive clear after std::move (same rationale as
    // completeApplyIfDone — see comment there).
    m_discardErrors.clear();
    m_discarding = false;
    Q_EMIT discardingChanged();
    Q_EMIT discardAllComplete(ok, errors);
    recomputeDirty();
}

void ApplicationController::forceResetAsyncState()
{
    // Emergency escape hatch — recovers from a state machine that's
    // stuck because both the terminal result signal AND the
    // destroyed() guard failed to fire (should be unreachable but
    // chrome buttons would be permanently disabled if it did).
    //
    // Bumping the generation counter is essential: the per-domain
    // applyResult/destroyed lambdas from the wedged batch are STILL
    // WIRED on the domains. If those domains later fire either signal
    // after this reset, the lambdas' generation guard will see the
    // new value and bail — without the bump, a stale fire after
    // reset would corrupt whatever state-machine state happens to be
    // live (the NEXT batch's counters, most likely).
    if (m_applying) {
        ++m_applyGeneration;
        m_applyPending = 0;
        m_applyOutstanding.clear();
        if (m_applyErrors.isEmpty())
            m_applyErrors.append(QStringLiteral("Async apply state force-reset"));
        completeApplyIfDone();
    }
    if (m_discarding) {
        ++m_discardGeneration;
        m_discardPending = 0;
        m_discardOutstanding.clear();
        if (m_discardErrors.isEmpty())
            m_discardErrors.append(QStringLiteral("Async discard state force-reset"));
        completeDiscardIfDone();
    }
}

void ApplicationController::setAsyncBatchTimeoutMs(int ms)
{
    if (ms <= 0) {
        qWarning() << "ApplicationController::setAsyncBatchTimeoutMs: refusing non-positive value" << ms
                   << "— keeping current" << m_asyncBatchTimeoutMs;
        return;
    }
    m_asyncBatchTimeoutMs = ms;
}

void ApplicationController::resetCurrentPage()
{
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

QStringList ApplicationController::parentChainFor(const QString& id) const
{
    // Cycle guard for parent-id graph walks. 32 hops is well above any
    // realistic sidebar nesting (typical settings apps cap at 3-4 levels)
    // and well below the cost of any pathological cycle.
    constexpr int kMaxParentChainHops = 32;

    QStringList chain;
    QString cursor = id;
    // Walk parent links upward; cap at kMaxParentChainHops as a cycle guard.
    for (int i = 0; i < kMaxParentChainHops; ++i) {
        if (!m_registry->hasPage(cursor)) {
            // Unknown id is a legitimate query path (QML probing during
            // bootstrap before pages are registered). Return whatever
            // we've collected so far without warning — the warning below
            // is reserved for actual cycle / depth-exceeded cases.
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
    // indicates a cycle in the registry's parentId graph (programmer
    // error). Warn so the bug surfaces instead of silently producing a
    // truncated chain.
    qWarning() << "ApplicationController::parentChainFor: cycle in registry, or depth exceeded the"
               << kMaxParentChainHops << "hop cap walking up from id" << id;
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
    if (m_recomputingDirty) {
        return;
    }
    m_recomputingDirty = true;
    auto guard = qScopeGuard([this]() {
        m_recomputingDirty = false;
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

} // namespace PhosphorSettingsUi

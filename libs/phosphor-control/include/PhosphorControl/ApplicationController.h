// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "PhosphorControl/PageRegistry.h"
#include "phosphorcontrol_export.h"

namespace PhosphorControl {

class PageController;
class StagingDomain;

/**
 * Top-level orchestrator for a phosphor-control application.
 *
 * Owns the PageRegistry, the set of registered StagingDomains (including
 * the ones embedded in PageControllers), and the current page selection.
 *
 * Recomputes a global dirty flag whenever any domain's dirtyChanged()
 * fires, drives applyAll() / discardAll() across all domains, and dispatches
 * a per-page resetToDefaults() to the current page only.
 *
 * Apps typically subclass this to declare their pages in the constructor.
 * Note that titles passed to registerPage must already be translated by
 * the caller (the subclass uses its own QObject::tr() context — this
 * library deliberately does not provide a translation context for app
 * strings):
 *
 *   auto *page = new MyPage(this);
 *   registerPage(page, {}, MyApp::tr("My Page"), QUrl("qrc:/MyPage.qml"));
 */
class PHOSPHORCONTROL_EXPORT ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PhosphorControl::PageRegistry* registry READ registry CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId NOTIFY currentPageIdChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool applying READ isApplying NOTIFY applyingChanged)
    Q_PROPERTY(bool discarding READ isDiscarding NOTIFY discardingChanged)
    QML_NAMED_ELEMENT(ApplicationController)
    QML_UNCREATABLE("ApplicationController is constructed in C++.")

public:
    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;

    PageRegistry* registry() const;

    bool isDirty() const;

    /// True while applyAllAsync is in flight (at least one domain's
    /// applyResult hasn't landed yet). UnsavedChangesFooter binds to
    /// this to swap "Save" → "Saving…" with the action disabled.
    bool isApplying() const;
    /// True while discardAllAsync is in flight — symmetric with
    /// isApplying.
    bool isDiscarding() const;

    QString currentPageId() const;
    void setCurrentPageId(const QString& id);

    /** Browser-style navigation history over `currentPageId`.
     *
     *  Every ORDINARY page change (sidebar click, deep link, CLI --page,
     *  gotoPrevious/NextPage, …) pushes the page it left onto the back
     *  stack and clears the forward stack — exactly the browser model.
     *  `goBack()` / `goForward()` move along the recorded trail without
     *  re-recording (they shuffle entries between the two stacks instead).
     *  Both return the page id landed on, or an empty string when there
     *  was nowhere to go. Stale entries are skipped and dropped: pages
     *  unregistered after being visited, and pages the current
     *  simple/advanced tier hides (landing on one would let the app's mode
     *  gate redirect us back out, which re-records the entry and corrupts
     *  both stacks). History depth is capped at kMaxHistoryEntries; the
     *  oldest entry falls off first. */
    bool canGoBack() const;
    bool canGoForward() const;
    Q_INVOKABLE QString goBack();
    Q_INVOKABLE QString goForward();

    /** Deep-link reveal latch (generic — every settings app built on this
     *  shell gets deep-link-to-anchor for free).
     *
     *  `setPendingAnchor` stashes a transient "reveal this anchor once its
     *  page is built and current" request. `PageHost` consumes it via
     *  `takePendingAnchor()` when the target page's Loader is ready + current
     *  (and immediately when the page is already active, via the
     *  `pendingAnchorChanged` signal), then invokes the page item's
     *  `revealAnchor(anchor)` contract. The pending request is cleared
     *  automatically when navigation moves to a different page
     *  (discard-on-navigate-away), so a never-reached deep link never fires
     *  on the wrong page. The fragment is NOT part of `currentPageId` — page
     *  identity stays fragment-free so existing id comparisons are unaffected. */
    void setPendingAnchor(const QString& pageId, const QString& anchor);
    /// Returns + clears the pending anchor iff it targets `pageId`; otherwise
    /// returns an empty string (consume-once).
    Q_INVOKABLE QString takePendingAnchor(const QString& pageId);

    /** Register a page in the sidebar. The page is also tracked as a
     *  staging domain. The controller's parent is reassigned to this
     *  ApplicationController if it has none.
     *
     *  When `isCollapsible` is true the sidebar renders this entry as
     *  an inline-expandable category header rather than a drill-down
     *  target — children appear indented under it instead of replacing
     *  the list.
     *
     *  When `hasDividerAfter` is true the sidebar draws a horizontal
     *  divider line immediately after this row (suppressed while a
     *  search filter is active). Used for visual grouping of long
     *  flat sections.
     *
     *  `visibility` declares the page's simple/advanced tier at
     *  registration — the canonical way to classify a page (see
     *  PageRegistry::PageVisibility). `counterpartId` names the page's
     *  other-mode equivalent for mode-flip redirects (see
     *  PageRegistry::Entry::counterpartId); it may reference a page
     *  registered later, so it is stored unvalidated. */
    void registerPage(PageController* page, const QString& parentId, const QString& title, const QUrl& qmlSource,
                      const QString& iconSource = QString(), bool isCollapsible = false, bool hasDividerAfter = false,
                      PageRegistry::PageVisibility visibility = PageRegistry::PageVisibility::Always,
                      const QString& counterpartId = QString());

    /** Register a headless staging domain (no sidebar entry).
     *  Used for cross-cutting state shared across multiple pages. */
    void registerDomain(StagingDomain* domain);

    /** Navigate to the previous / next navigable page (one with a
     *  qmlSource) in the registry's in-order traversal. The current
     *  page is skipped; the list wraps at boundaries. Returns the id
     *  we landed on, or an empty string if there's nowhere to go.
     *
     *  When the current page is not in the navigable list (e.g. unset
     *  at startup or pointing at a non-navigable category), gotoPrevious
     *  wraps to the last page and gotoNext wraps to the first. */
    Q_INVOKABLE QString gotoPreviousPage();
    Q_INVOKABLE QString gotoNextPage();

    /** Walk the parent chain from the registered entry for `id` to the
     *  top level. Returns the chain ordered root-first, EXCLUDING `id`
     *  itself. Used by consumers to restore sidebar drill state at
     *  startup (e.g. given a restored activePage of
     *  "snapping-behavior", produces ["snapping"]). */
    Q_INVOKABLE QStringList parentChainFor(const QString& id) const;

public Q_SLOTS:
    void applyAll();
    void discardAll();
    void resetCurrentPage();
    /// Async variant of applyAll — collects each dirty domain's
    /// applyResult signal and emits applyAllComplete(ok, errors) when
    /// all responses have landed. Chrome should prefer this over
    /// applyAll so a stuck D-Bus call doesn't freeze the GUI thread.
    /// Domains whose apply() is fully synchronous still emit
    /// applyResult immediately, so they complete on the same event-
    /// loop turn — the async path degenerates to "sync + one tail
    /// signal" for them.
    void applyAllAsync();
    /// Symmetric to applyAllAsync; emits discardAllComplete(ok, errors).
    void discardAllAsync();
    /// Force-reset the async-batch state machine to idle.
    /// Recovers from the (hopefully-impossible) case where a domain
    /// failed to emit applyResult/discardResult AND its destroyed()
    /// signal also never fired (e.g. an exception unwound past Qt's
    /// child-deletion). Emits the matching *Complete signal with
    /// ok=false so observers are notified. QML escape hatch.
    void forceResetAsyncState();

public:
    /// Async-batch timeout in milliseconds. Default 60 000 ms is
    /// generous for typical D-Bus chains (~500 ms replies) but tight
    /// enough to surface a wedged backend instead of pinning the
    /// chrome's "Saving…" state indefinitely. Consumers with shorter
    /// SLOs (interactive feedback within 5 s) or longer migrations
    /// can adjust before the first applyAllAsync. Must be > 0.
    int asyncBatchTimeoutMs() const;
    void setAsyncBatchTimeoutMs(int ms);

Q_SIGNALS:
    void dirtyChanged();
    void currentPageIdChanged();
    /// Emitted when canGoBack / canGoForward may have flipped (shared
    /// NOTIFY for both properties — they always change together or not
    /// at all from the QML binding system's perspective).
    void historyChanged();
    /// Emitted when the deep-link pending anchor is set or cleared. PageHost
    /// listens so an already-current page reveals immediately.
    void pendingAnchorChanged();
    /// Emitted on every applying-state transition (false→true at the
    /// start of applyAllAsync, true→false when the last applyResult
    /// has landed). UnsavedChangesFooter binds Save button text +
    /// enabled state to this.
    void applyingChanged();
    /// Symmetric to applyingChanged for discardAllAsync.
    void discardingChanged();
    /// Emitted exactly once per applyAllAsync invocation, when every
    /// dirty domain's applyResult has been received. `ok` is true iff
    /// every domain reported success. `errors` is a list of
    /// user-readable messages (one per failed domain).
    void applyAllComplete(bool ok, const QStringList& errors);
    /// Symmetric to applyAllComplete for discardAllAsync.
    void discardAllComplete(bool ok, const QStringList& errors);

private Q_SLOTS:
    void onDomainDirtyChanged();

private:
    void trackDomain(StagingDomain* domain);
    void recomputeDirty();
    /// Helpers used by the async batches. Set the matching m_*Pending
    /// counter to N, hook a one-shot lambda per domain, then call
    /// apply()/discard(). When the counter reaches zero, the helper
    /// emits applyAllComplete / discardAllComplete and toggles the
    /// applying/discarding state back to false.
    void completeApplyIfDone();
    void completeDiscardIfDone();

    // All POD-like members default-initialised here for uniformity —
    // m_registry is assigned in the ctor body for clarity (it depends
    // on `this` as its QObject parent).
    PageRegistry* m_registry = nullptr;
    QList<QPointer<StagingDomain>> m_domains;
    QString m_currentPageId;
    // Back/forward navigation history (see canGoBack/goBack). The back
    // stack's last entry is the page goBack() lands on; symmetric for
    // the forward stack. m_navigatingHistory suppresses recording while
    // goBack/goForward drive setCurrentPageId, so history moves don't
    // re-record themselves.
    QStringList m_backHistory;
    QStringList m_forwardHistory;
    bool m_navigatingHistory = false;
    /// History depth cap — plenty for a settings session while bounding
    /// worst-case memory for a long-lived window.
    static constexpr int kMaxHistoryEntries = 64;
    // Deep-link reveal latch (see setPendingAnchor). Transient; not page identity.
    QString m_pendingAnchor;
    QString m_pendingAnchorPage;
    bool m_dirty = false;
    // Set during applyAll / discardAll so each inner dirtyChanged →
    // recomputeDirty edge is suppressed; the outer transaction emits
    // a single recomputeDirty at the end. Eliminates the O(N²) walk
    // pattern audit-follow-up A15 flagged.
    bool m_inTransaction = false;
    // Re-entrancy guard for recomputeDirty. A subclass that emits
    // dirtyChanged from inside its isDirty() virtual would otherwise
    // recursively re-enter the QList::erase loop, undefined behaviour.
    bool m_recomputingDirty = false;
    // Set when recomputeDirty is rejected by the re-entrancy guard;
    // the outer call replays the recompute via a queued invokeMethod
    // so a cascaded dirty edge isn't silently swallowed.
    bool m_recomputeDirtyPending = false;

    // Async batch state. m_*Pending counts how many domains still
    // owe an applyResult / discardResult; m_*Errors collects user-
    // readable failure messages.
    int m_applyPending = 0;
    QStringList m_applyErrors;
    bool m_applying = false;
    int m_discardPending = 0;
    QStringList m_discardErrors;
    bool m_discarding = false;
    // Monotonic batch generation counters. Bumped at the start of each
    // *Async batch (and inside forceResetAsyncState) and captured by
    // every lambda the batch wires — the lambda bails out when its
    // captured generation no longer matches the live counter. Guards
    // against ALL stale-callback shapes:
    //   * a 60s timer firing after a previous batch completed and a
    //     new batch is now in flight
    //   * a per-domain applyResult/destroyed lambda from a re-entered
    //     batch decrementing the NEW batch's pending counter
    //   * an applyResult fired after forceResetAsyncState recovered a
    //     wedged batch (the stale lambdas are still wired but the
    //     generation has moved on)
    quint64 m_applyGeneration = 0;
    quint64 m_discardGeneration = 0;
    // Per-batch outstanding domain sets. A domain is in the set when
    // we still expect EXACTLY ONE more terminal signal (applyResult
    // OR destroyed) from it. The first to fire removes the domain;
    // the second is a no-op. Without this, a domain that emits
    // applyResult synchronously and is then destroyed later in the
    // batch would tick the pending counter TWICE (once per signal),
    // driving it negative and miscounting completion.
    //
    // Pointer-deref safety: the per-domain `destroyed` lambda removes
    // the dying pointer from this set BEFORE the destructor returns,
    // and the batch-timeout handler (the only other consumer that
    // dereferences these pointers to read `objectName()`) walks the
    // set only AFTER all live entries have been processed — by which
    // point the set contains only pointers to still-live domains.
    // No dangling deref is reachable along the documented call paths,
    // but callers that introduce a new consumer of these pointers
    // MUST honour the same single-threaded "remove-before-destroy"
    // discipline.
    QSet<StagingDomain*> m_applyOutstanding;
    QSet<StagingDomain*> m_discardOutstanding;
    // Per-batch connection handles for the applyResult / destroyed
    // (apply path) and discardResult / destroyed (discard path) lambdas.
    // Cleared and disconnected at terminal emit in completeApplyIfDone /
    // completeDiscardIfDone so dead lambdas don't accumulate on domains
    // that completed this batch but never participate in subsequent
    // batches. Qt::SingleShotConnection self-disconnects only on actual
    // fire; the destroyed() lambda for a long-lived domain would
    // otherwise stay wired across every batch's lifetime.
    QList<QMetaObject::Connection> m_applyConnections;
    QList<QMetaObject::Connection> m_discardConnections;
    /// Hard-cap on how long an async batch waits for terminal result
    /// signals before synthesising a failure entry per still-pending
    /// domain. Default 60 seconds (see asyncBatchTimeoutMs() /
    /// setAsyncBatchTimeoutMs() for consumer-facing tuning).
    static constexpr int kDefaultAsyncBatchTimeoutMs = 60'000;
    int m_asyncBatchTimeoutMs = kDefaultAsyncBatchTimeoutMs;
};

} // namespace PhosphorControl

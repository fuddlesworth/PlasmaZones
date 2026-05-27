// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "PhosphorSettingsUi/PageRegistry.h"
#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

class PageController;
class StagingDomain;

/**
 * Top-level orchestrator for a phosphor-settings-ui application.
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
class PHOSPHORSETTINGSUI_EXPORT ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PhosphorSettingsUi::PageRegistry* registry READ registry CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentPageId READ currentPageId WRITE setCurrentPageId NOTIFY currentPageIdChanged)
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
     *  flat sections. */
    void registerPage(PageController* page, const QString& parentId, const QString& title, const QUrl& qmlSource,
                      const QString& iconSource = QString(), bool isCollapsible = false, bool hasDividerAfter = false);

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

public:
    /// Async variant of applyAll — collects each dirty domain's
    /// applyResult signal and emits applyAllComplete(ok, errors) when
    /// all responses have landed. Chrome should prefer this over
    /// applyAll so a stuck D-Bus call doesn't freeze the GUI thread.
    /// Domains whose apply() is fully synchronous still emit
    /// applyResult immediately, so they complete on the same event-
    /// loop turn — the async path degenerates to "sync + one tail
    /// signal" for them.
    ///
    /// Q_INVOKABLE (not slot) — async-batch state is too stateful for
    /// arbitrary signal wiring; QML calls it directly and that's the
    /// only intended caller surface. The sync slots above accept
    /// signal wiring for legacy compatibility.
    Q_INVOKABLE void applyAllAsync();
    /// Symmetric to applyAllAsync; emits discardAllComplete(ok, errors).
    Q_INVOKABLE void discardAllAsync();
    /// Force-reset the async-batch state machine to idle.
    /// Recovers from the (hopefully-impossible) case where a domain
    /// failed to emit applyResult/discardResult AND its destroyed()
    /// signal also never fired (e.g. an exception unwound past Qt's
    /// child-deletion). Emits the matching *Complete signal with
    /// ok=false so observers are notified. QML escape hatch.
    Q_INVOKABLE void forceResetAsyncState();

Q_SIGNALS:
    void dirtyChanged();
    void currentPageIdChanged();
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
    bool m_dirty = false;
    // Set during applyAll / discardAll so each inner dirtyChanged →
    // recomputeDirty edge is suppressed; the outer transaction emits
    // a single recomputeDirty at the end. Eliminates the O(N²) walk
    // pattern audit-follow-up A15 flagged.
    bool m_inTransaction = false;

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
    // *Async batch and captured by that batch's timeout lambda — when
    // the timer fires, the lambda compares its captured generation
    // against the current value and bails out if they don't match.
    // Prevents a stale timer from a previous batch from spuriously
    // triggering against a *new* batch the user kicked off after the
    // first one completed (60s gap is unlikely but possible).
    quint64 m_applyGeneration = 0;
    quint64 m_discardGeneration = 0;
    /// Hard-cap on how long an async batch waits for terminal result
    /// signals before synthesising a failure entry per still-pending
    /// domain. 60 seconds is generous for D-Bus chains (typical
    /// reply &lt; 500 ms) but tight enough that a fully-wedged backend
    /// surfaces a visible failure rather than wedging the chrome
    /// indefinitely.
    static constexpr int kAsyncBatchTimeoutMs = 60'000;
};

} // namespace PhosphorSettingsUi

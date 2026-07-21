// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Active-page navigation + per-page dirty tracking + external-edit envelope
// for SettingsController:
//   * setActivePage / resolveToLeaf / firstLeafAnyMode — switch the viewed
//     page, resolving a parent category to a leaf and applying the
//     simple/advanced mode gate
//   * navigateTo      — setActivePage plus an optional "#anchor" reveal
//   * setAdvancedMode — flip the mode, re-filter the rail, re-gate the page
//   * onSettings/ExternalSettingsChanged — NOTIFY → dirty / reload hooks
//   * maybeDrainPendingExternalReload — adopt a reload deferred while dirty
//   * setNeedsSave / dirtyPages / isPageDirty — dirty-state surface for QML
//   * emitDirtyPagesChanged — the batched dirtyPagesChanged emit
//   * syncDirtyMembership — the one m_dirtyPages insert/remove invariant
//   * reconcilePagesDirty / reconcilePageDirty / reconcileRuleBackedDirty —
//     re-attribute dirty state from the value-based truth
//   * begin/endExternalEdit — stack envelope so sidebar/global widgets mark
//     the correct page dirty
//
// Per-page Reset/Discard moved to the sibling settingscontroller_pagereset.cpp
// when this file passed the 1150-line ceiling; the page-class predicates and
// shared-domain key lists both halves use live in settingscontroller_pagekeys.h.
//
// Same class as settingscontroller.cpp, separate TU, no API change.

#include "settingscontroller.h"

#include "animationpagescope.h"
#include "decorationpagescope.h"
#include "settingscontroller_pagekeys.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QDebug>
#include <QTimer>

namespace PlasmaZones {

void SettingsController::emitDirtyPagesChanged()
{
    if (m_dirtyEmitDepth > 0) {
        m_dirtyEmitPending = true;
        return;
    }
    Q_EMIT dirtyPagesChanged();
}

void SettingsController::navigateTo(const QString& address)
{
    // Split the optional "#anchor" fragment. The page part flows through the
    // normal setActivePage path (parent→leaf redirect + dirty handling +
    // currentPageId sync); the fragment is keyed to the RESOLVED leaf so a
    // parent-id address still reveals on the leaf it redirects to. A
    // fragment-free address behaves byte-for-byte like setActivePage.
    const int hash = address.indexOf(QLatin1Char('#'));
    const QString page = (hash < 0) ? address : address.left(hash);
    const QString anchor = (hash < 0) ? QString() : address.mid(hash + 1);

    setActivePage(page);

    // Key the anchor to the resolved leaf, and only when that leaf is what
    // setActivePage actually landed on — the mode gate may have redirected a
    // hidden target to its counterpart (or Overview), where this anchor's
    // content does not exist; stashing it there would leave a stale pending
    // reveal for the next visit. A bogus address is rejected the same way.
    if (!anchor.isEmpty() && app() != nullptr) {
        // Resolved lazily, inside the guard: resolveToLeaf walks the page tree
        // recursively for a category address, and an anchor-free navigateTo
        // (every sidebar click, every --page, every D-Bus call) has no use for
        // the result. setActivePage does its own resolve internally.
        const QString resolved = resolveToLeaf(page);
        if (validPageNames().contains(resolved) && m_activePage == resolved) {
            app()->setPendingAnchor(resolved, anchor);
        }
    }
}

// First navigable leaf below `parentId` in registration order, IGNORING the
// simple/advanced tier. Fallback for resolveToLeaf when a category's whole
// subtree is hidden by the current mode — the resolved leaf then trips the
// mode gate, which redirects to its counterpart or Overview.
static QString firstLeafAnyMode(const PhosphorControl::PageRegistry* registry, const QString& parentId)
{
    const auto children = registry->childPages(parentId);
    for (const auto& child : children) {
        if (!child.qmlSource.isEmpty()) {
            return child.id;
        }
        const QString leaf = firstLeafAnyMode(registry, child.id);
        if (!leaf.isEmpty()) {
            return leaf;
        }
    }
    return {};
}

QString SettingsController::resolveToLeaf(const QString& page) const
{
    // Valid leaf names (and unknown ids — the caller's validPageNames check
    // owns that rejection) pass through untouched; only registered virtual
    // nodes need resolving. Before the registry exists there is nothing to
    // resolve against.
    if (validPageNames().contains(page) || !m_app || !m_app->registry() || !m_app->registry()->hasPage(page)) {
        return page;
    }
    const auto* registry = m_app->registry();
    // Mode-aware first: land on the first leaf the CURRENT rail can show,
    // so e.g. --page=animations reaches animations-simple in simple mode
    // and animations-general in advanced mode.
    const QString visible = registry->firstVisibleLeafId(page);
    if (!visible.isEmpty()) {
        return visible;
    }
    const QString any = firstLeafAnyMode(registry, page);
    return any.isEmpty() ? page : any;
}

void SettingsController::setActivePage(const QString& page)
{
    // Resolve parent category names (e.g. "snapping" → "snapping-overlay-behavior")
    // against the live registry topology and the current mode.
    const QString resolved = resolveToLeaf(page);

    if (!validPageNames().contains(resolved)) {
        // The page name arrives over D-Bus (SettingsAppAdaptor::setActivePage), so it is
        // caller-supplied and unbounded. A typo still deserves a trace, so keep one — at
        // debug rather than warning, which is what kept the log-flooding concern honest.
        // The NAME is echoed: a trace that cannot say which page was wrong carries none of
        // the information a typo trace exists for, and debug is off in production anyway.
        // Same treatment as the adaptor's unknown-key trace (settingsadaptor.cpp).
        qCDebug(PlasmaZones::lcCore) << "Unknown settings page requested:" << page;
        return;
    }
    // Mode gate: a deep link / CLI --page / D-Bus request, a stale history
    // entry, or a mode flip (setAdvancedMode re-runs this on the current page)
    // may target a page the active mode hides. Redirect onto a page the rail
    // can actually show rather than stranding the user on a hidden one.
    // Internal rail clicks always name a visible page, so this only fires for
    // out-of-band navigation and mode changes. Driven entirely by the
    // registry's per-page tier + counterpart declarations: a hidden page goes
    // to its declared other-mode counterpart when that is itself showable,
    // else to Overview (which is Always-visible, so there is no recursion).
    QString target = resolved;
    if (m_app && m_app->registry() && !m_app->registry()->pageAllowedInCurrentMode(target)) {
        const QString counterpart = m_app->registry()->entry(target).counterpartId;
        if (!counterpart.isEmpty() && validPageNames().contains(counterpart)
            && m_app->registry()->pageAllowedInCurrentMode(counterpart)) {
            qCDebug(PlasmaZones::lcCore) << "Page" << target << "is hidden in the current mode; redirecting to its"
                                         << "counterpart" << counterpart;
            target = counterpart;
        } else {
            // No counterpart. Before falling all the way back to Overview, walk
            // up the ancestry and take the nearest ancestor that still has a
            // visible leaf: a user on Animations → Windows who flips to simple
            // mode wants the simple Animations surface, not the dashboard.
            // Most AdvancedOnly pages declare no counterpart (only the three
            // condensed surfaces do), so without this the mode toggle throws
            // away the user's location for the majority of the catalogue.
            // Nearest-first: parentChainFor returns the chain root-first.
            QString fallback;
            const QStringList chain = m_app->parentChainFor(target);
            for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
                const QString leaf = m_app->registry()->firstVisibleLeafId(*it);
                // Re-test the gate's own predicate on the candidate.
                // firstVisibleLeafId filters DESCENDANTS but never asks whether
                // the ancestor it was handed is itself reachable, so for an
                // AdvancedOnly/SimpleOnly category it can hand back a leaf whose
                // ancestry is filtered — including the very target we are
                // redirecting away from. Every virtual parent is Always today,
                // so this cannot fire yet; checking here means it never can.
                if (!leaf.isEmpty() && validPageNames().contains(leaf)
                    && m_app->registry()->pageAllowedInCurrentMode(leaf)) {
                    fallback = leaf;
                    break;
                }
            }
            if (fallback.isEmpty()) {
                fallback = QStringLiteral("overview");
            }
            qCDebug(PlasmaZones::lcCore) << "Page" << target << "is hidden in the current mode; redirecting to"
                                         << fallback;
            target = fallback;
        }
    }
    // Reentrancy guard: a slot connected to activePageChanged that
    // calls setActivePage again (e.g. a CLI --page handler that
    // redirects to a fallback page) would otherwise re-trigger
    // m_loading toggling, leaving the toggle in an unspecified state
    // if the inner call set m_loading = false before the outer call's
    // restore ran. Returning early on re-entry keeps m_loading's
    // false→true→false window symmetric per public-entry call.
    if (m_settingActivePage) {
        qCWarning(PlasmaZones::lcCore) << "setActivePage: reentrant call refused (already setting active page to"
                                       << m_activePage << ")";
        return;
    }
    if (m_activePage != target) {
        // m_loading suppresses onSettingsPropertyChanged — the QML Loader
        // reacts synchronously to activePageChanged and new page creation
        // may trigger NOTIFY signals that would otherwise mark pages dirty.
        // Both flags are raised through the RAII scope rather than
        // hand-restored: the emit below synchronously runs arbitrary QML page
        // construction, and an exception escaping it would otherwise strand
        // m_loading raised (dirty tracking suppressed for the rest of the
        // session) and m_settingActivePage raised (every later navigation
        // refused as reentrant).
        const ScopedFlag activePageScope(m_settingActivePage);
        const ScopedFlag loadingScope(m_loading);
        m_activePage = target;
        Q_EMIT activePageChanged();
    }
}

void SettingsController::setAdvancedMode(bool advanced)
{
    if (m_advancedMode == advanced) {
        return;
    }
    m_advancedMode = advanced;
    // Pages bind card/row `visible:` to advancedMode — let them re-evaluate
    // first, then re-filter the rail.
    Q_EMIT advancedModeChanged();
    if (m_app && m_app->registry()) {
        m_app->registry()->setShowAdvanced(advanced);
    }
    // A mode flip can hide the page we're on (an advanced-only page when
    // entering simple, or the SimpleOnly animations page when entering
    // advanced). Re-run setActivePage against the current page: its gate
    // redirects to a visible page when hidden and no-ops when still visible.
    setActivePage(m_activePage);
}

void SettingsController::onSettingsPropertyChanged()
{
    // isApplyingSystemPalette(): a runtime ApplicationPaletteChange re-derive
    // (Settings::eventFilter) fires the zone-color NOTIFYs, but it is
    // palette-driven, not a user edit. Settings rebaselines the derived keys
    // itself, though only when the useSystemColors toggle is committed — a
    // pending (uncommitted) toggle keeps them discardable. Flipping needsSave
    // here would show a phantom unsaved-changes footer on every theme switch.
    if (!m_saving && !m_loading && !m_settings.isApplyingSystemPalette()) {
        setNeedsSave(true);
    }
}

void SettingsController::onExternalSettingsChanged()
{
    if (m_saving) {
        return;
    }
    // With unsaved edits on any page, a reload would reparse disk over the
    // in-memory Settings and end in setNeedsSave(false) — silently dropping
    // the user's pending changes and clearing the footer. Staging-domain
    // pages snapshot/revert their own state, but plain Q_PROPERTY edits
    // (e.g. the animation profile fields) have no such net. Keep the local
    // edits and remember the deferred reload: the next clean transition
    // resolves it — dirtyPagesChanged fires maybeDrainPendingExternalReload
    // (the per-page Discard path), while footer save/discard and defaults()
    // clear or subsume the flag via their own disk rewrite or load(). Either
    // way an externally-changed sibling page doesn't stay stale
    // indefinitely.
    if (needsSave()) {
        qCInfo(lcCore) << "External settings change deferred: unsaved local edits take precedence";
        m_pendingExternalReload = true;
        return;
    }
    load();
}

void SettingsController::setNeedsSave(bool needs)
{
    // Mark the target page as dirty, or clear all dirty pages if needs ==
    // false. The target is the top of the external-edit stack when set
    // (sidebar / global widgets that mutate settings owned by a different
    // page than the one the user is viewing), otherwise m_activePage.
    // Parent categories ("snapping", "tiling") are never the active page —
    // setActivePage redirects them to their first child — so the target
    // always resolves to a concrete leaf page.
    if (needs) {
        // Walk down to the topmost entry that names a page. beginExternalEdit
        // pushes an empty sentinel when its id does not resolve, so the stack
        // stays balanced against endExternalEdit's unconditional pop; such a
        // scope contributes no target of its own and must fall through to the
        // enclosing one (or m_activePage) rather than steal or erase it.
        QString target = m_activePage;
        for (auto it = m_externalEditStack.crbegin(); it != m_externalEditStack.crend(); ++it) {
            if (!it->isEmpty()) {
                target = *it;
                break;
            }
        }
        // The target must resolve to a concrete leaf page; a parent-category id
        // (registered in the page tree but not a navigable leaf) would poison
        // m_dirtyPages with a page the user never directly edits. Assert in
        // debug, and in release skip the insert rather than dirtying a
        // redirect target.
        const bool isVirtualNode =
            m_app && m_app->registry() && m_app->registry()->hasPage(target) && !validPageNames().contains(target);
        Q_ASSERT(!isVirtualNode);
        if (isVirtualNode) {
            return;
        }
        if (syncDirtyMembership(target, true)) {
            emitDirtyPagesChanged();
        }
    } else if (!m_dirtyPages.isEmpty()) {
        m_dirtyPages.clear();
        emitDirtyPagesChanged();
    }
}

void SettingsController::maybeDrainPendingExternalReload()
{
    // Drain a reload that onExternalSettingsChanged() deferred while edits
    // were pending. Wired to dirtyPagesChanged (the one signal every
    // clean-transition path emits — footer save/discard via setNeedsSave,
    // per-page kebab Discard via reconcilePageDirty, the virtual-screens
    // branch's direct removal), so an externally-changed sibling page is
    // adopted as soon as the app is fully clean instead of staying stale.
    // Queued (not inline): this fires inside save()/discard flows, and the
    // re-entry goes back through onExternalSettingsChanged() so the
    // m_saving guard and the dirty check re-evaluate at fire time.
    if (!m_pendingExternalReload || needsSave() || m_saving || m_loading) {
        return;
    }
    m_pendingExternalReload = false;
    QTimer::singleShot(0, this, &SettingsController::onExternalSettingsChanged);
}

QStringList SettingsController::dirtyPages() const
{
    // Order is unspecified — QML uses this only as a binding dependency
    // and calls isPageDirty() for the actual lookup.
    return QStringList(m_dirtyPages.begin(), m_dirtyPages.end());
}

bool SettingsController::isPageDirty(const QString& page) const
{
    // Manifest-backed leaf: value-based — dirty iff any owned config key
    // differs from the committed baseline. This stays correct across a
    // per-page Discard/Reset (which mutate the store directly) without relying
    // on the m_dirtyPages active-page heuristic.
    const auto& manifest = pageOwnedConfigKeys();
    const auto ownedIt = manifest.constFind(page);
    if (ownedIt != manifest.constEnd()) {
        for (const auto& gk : *ownedIt) {
            if (m_settings.isKeyModified(gk.first, gk.second))
                return true;
        }
        return false;
    }

    // Condensed SimpleOnly pages own no keys — their dirty state is the
    // value-based union of their backing advanced pages, so a revert through
    // either surface (this page's kebab, the backing page's kebab, or a
    // global Discard) reads the same truth. Checked BEFORE the m_dirtyPages
    // heuristic so a stale active-page entry cannot keep a clean simple page
    // reporting dirty.
    //
    // Positioned directly after the manifest to match resetPage and
    // discardPage, which both check backing pages second. The three only agree
    // today because no page is in both the backing map and a shared-domain
    // group; the moment one is, a different order here would make the dirty
    // check take the shared-domain branch while Reset and Discard delegate.
    {
        const auto& backing = simplePageBackingPages();
        const auto backingIt = backing.constFind(page);
        if (backingIt != backing.constEnd()) {
            for (const QString& backingPage : *backingIt) {
                if (isPageDirty(backingPage))
                    return true;
            }
            return false;
        }
    }

    // Ordering pages: dirty iff a custom order is staged that differs from the
    // saved order (a staged value equal to the saved order is not a change).
    // Dispatched on the SHARED classifier so adding a third ordering page is a
    // new enumerator this switch (and Reset, and Discard) must answer for,
    // rather than a page that quietly falls into the tiling branch.
    switch (orderingPageKind(page)) {
    case OrderingPageKind::Snapping:
        return m_stagedSnappingOrder.has_value() && *m_stagedSnappingOrder != m_settings.snappingLayoutOrder();
    case OrderingPageKind::Tiling:
        return m_stagedTilingOrder.has_value() && *m_stagedTilingOrder != m_settings.tilingAlgorithmOrder();
    case OrderingPageKind::None:
        break;
    }

    // Quick Shortcuts pages: dirty iff a quick-slot edit is staged for the
    // mode. Same shared-classifier gate as the ordering pages above.
    if (isShortcutsPage(page)) {
        return page == QLatin1String("snapping-shortcuts") ? m_staging.hasStagedSnappingQuickSlots()
                                                           : m_staging.hasStagedTilingQuickSlots();
    }

    // Virtual Screens page: dirty iff any physical screen has a staged
    // virtual-screen edit (a split or a removal).
    if (page == QLatin1String("virtualscreens")) {
        return m_staging.hasStagedVirtualScreenConfigs();
    }

    // Animation pages share one staging domain and one ShaderProfileTree key, but
    // dirty is value-based PER SCOPE so a revert on one surface never lights
    // another's badge (mirrors the decoration domain below). A surface leaf is
    // dirty iff its own event subtree carries a staged override FILE or its shader
    // tree diverges from baseline within scope; General is dirty iff its config
    // keys diverge; the library leaves fall back to whole-tree (any file or key
    // edit shows there).
    if (isAnimationPage(page)) {
        const AnimationPageScope scope = animationPageScope(page);
        if (scope.kind == AnimationPageScope::ConfigOnly) {
            for (const auto& gk : animationGeneralConfigKeys()) {
                if (m_settings.isKeyModified(gk.first, gk.second))
                    return true;
            }
            return false;
        }
        if (scope.kind == AnimationPageScope::EventSubtree) {
            if (m_animationsPage != nullptr
                && m_animationsPage->hasScopedPendingFiles(animationScopedBuiltInPaths(scope)))
                return true;
            // A page hosting the global timing / filter cards is also dirty
            // when those keys diverge (the condensed simple page).
            if (scope.includeGeneralKeys) {
                for (const auto& gk : animationGeneralConfigKeys()) {
                    if (m_settings.isKeyModified(gk.first, gk.second))
                        return true;
                }
            }
            return shaderTreeScopeDiffers(m_settings.shaderProfileTree(), m_settings.committedShaderProfileTree(),
                                          scope);
        }
        // WholeTree library leaves (presets / motion sets / shaders).
        if (m_animationsPage != nullptr && m_animationsPage->hasPendingChanges())
            return true;
        for (const auto& gk : animationConfigKeys()) {
            if (m_settings.isKeyModified(gk.first, gk.second))
                return true;
        }
        return false;
    }

    // Decoration pages share the single DecorationProfileTree key but are
    // value-based per SURFACE ROOT: a surface page (windows/osds/popups) is dirty
    // iff its own root subtree differs from the committed baseline, so a revert on
    // one surface never lights another's badge. The non-surface leaves (sets
    // library, read-only shaders browser) have no root of their own, so they fall
    // back to whole-tree dirty — any decoration edit shows there. The
    // manifest-owned window-appearance leaf is handled by the manifest branch
    // above.
    if (isDecorationPage(page)) {
        const QString root = decorationSurfaceRoot(page);
        if (!root.isEmpty()) {
            return decorationRootDiffers(m_settings.decorationProfileTree(),
                                         m_settings.committedDecorationProfileTree(), root);
        }
        for (const auto& gk : decorationConfigKeys()) {
            if (m_settings.isKeyModified(gk.first, gk.second))
                return true;
        }
        return false;
    }

    if (m_dirtyPages.contains(page))
        return true;
    // Parent / virtual-parent category: dirty if any child leaf in
    // the group is dirty. Single direct-membership lookup against
    // `pageGroupChildren()` rather than the old prefix-walk-or-hash-
    // lookup branch — top-level parents (snapping / tiling /
    // animations) and virtual mid-level parents (animations-transitions /
    // animations-motion / animations-library) share the same code path now.
    // Recurse through
    // isPageDirty (not a bare m_dirtyPages lookup) so a manifest-backed child
    // contributes its value-based dirty state to the collapsed parent badge.
    // Condensed simple pages that DECLARE backing pages are skipped: their
    // dirty state is by definition the union of backing pages that are
    // siblings in this same set, so visiting them re-walks those pages' key
    // lists for no new information (this is a hot path — see
    // pageGroupChildren's note). animations-simple is deliberately not in the
    // backing map — it scopes its own event roots rather than delegating — so
    // it is visited like any other leaf.
    const auto& groups = pageGroupChildren();
    const auto it = groups.constFind(page);
    if (it != groups.constEnd()) {
        const auto& backing = simplePageBackingPages();
        for (const QString& child : *it) {
            if (backing.contains(child))
                continue;
            if (isPageDirty(child))
                return true;
        }
    }
    return false;
}

void SettingsController::reconcilePagesDirty(const QSet<QString>& pages)
{
    // Batched reconcilePageDirty: adjust every page's m_dirtyPages membership
    // against its value-based truth, then emit dirtyPagesChanged AT MOST once —
    // the shared-domain reset paths reconcile every leaf of a group and would
    // otherwise fire one NOTIFY per flipped leaf (the discard paths already
    // batch this way).
    bool changed = false;
    for (const QString& page : pages) {
        if (syncDirtyMembership(page, isPageDirty(page))) {
            changed = true;
        }
    }
    if (changed) {
        emitDirtyPagesChanged();
    }
}

void SettingsController::reconcilePageDirty(const QString& page)
{
    // Match m_dirtyPages to the value-based truth for this manifest page,
    // then cascade to any condensed simple page backed by it: a revert on
    // snapping-overlay-behavior must also clear a stale snapping-simple
    // entry (the simple leaf is where setNeedsSave attributed the edit while
    // the user was in simple mode). Batched into one NOTIFY.
    bool changed = false;
    const auto sync = [this, &changed](const QString& p) {
        if (syncDirtyMembership(p, isPageDirty(p))) {
            changed = true;
        }
    };
    sync(page);
    const auto& backing = simplePageBackingPages();
    for (auto it = backing.constBegin(); it != backing.constEnd(); ++it) {
        if (it.value().contains(page))
            sync(it.key());
    }
    if (changed) {
        emitDirtyPagesChanged();
    }
}

void SettingsController::reconcileRuleBackedDirty()
{
    if (m_rulesPage == nullptr)
        return;
    // The Rules page stages user rules into the shared RuleController model;
    // attribute its dirty state from the value-based user-rules query so a user-rule
    // edit badges Rules even when the shared dirty bit does not transition. (The
    // Windows appearance page is a plain config page now — its dirtiness comes from
    // the config manifest via isPageDirty, not from the rule model.)
    if (syncDirtyMembership(QStringLiteral("rules"), m_rulesPage->userRulesDirty())) {
        emitDirtyPagesChanged();
    }
}

bool SettingsController::syncDirtyMembership(const QString& page, bool dirty)
{
    if (dirty) {
        if (m_dirtyPages.contains(page)) {
            return false;
        }
        m_dirtyPages.insert(page);
        return true;
    }
    return m_dirtyPages.remove(page);
}

void SettingsController::beginExternalEdit(const QString& page)
{
    // Resolve parent categories to their canonical leaf — same rules as
    // setActivePage — so the sidebar can pass "snapping" or "tiling".
    const QString resolved = resolveToLeaf(page);
    if (!validPageNames().contains(resolved)) {
        qCWarning(PlasmaZones::lcCore) << "beginExternalEdit: unknown page" << page;
        // Push an empty sentinel anyway. ExternalEditScope's destructor calls
        // endExternalEdit unconditionally, so returning without pushing would
        // pop an entry this scope never owned: with an enclosing envelope open
        // it would steal that envelope's target and misattribute every later
        // edit, and with none it would trip endExternalEdit's unbalanced-pop
        // assert. An empty entry names no page, so setNeedsSave skips past it
        // to the enclosing target.
        m_externalEditStack.push(QString());
        return;
    }
    // Push onto the stack so nested begin/end pairs restore the outer
    // target on pop instead of clearing the wrap entirely. This is
    // genuinely reachable: an animations-page pendingChangesChanged
    // handler can fire synchronously while the controller is inside a
    // rules-driven external-edit envelope, and the inner pair
    // must not erase the outer target.
    m_externalEditStack.push(resolved);
}

void SettingsController::endExternalEdit()
{
    if (m_externalEditStack.isEmpty()) {
        // Defence-in-depth: an unmatched end means a begin was lost or
        // a caller is double-popping. Warn so the failure is visible
        // instead of silently no-oping (the previous QString-clear
        // form was equally silent, but a stack pop on empty would
        // crash in debug builds without this guard).
        qCWarning(PlasmaZones::lcCore) << "endExternalEdit: stack is empty — unmatched end?";
        Q_ASSERT_X(false, "SettingsController::endExternalEdit",
                   "endExternalEdit called with no matching beginExternalEdit on the stack.");
        return;
    }
    // Drops empty sentinels the same way as real targets: the sentinel exists
    // only to keep this pop paired with a begin that could not resolve its id,
    // and it already carries "no target" for setNeedsSave.
    m_externalEditStack.pop();
}

} // namespace PlasmaZones

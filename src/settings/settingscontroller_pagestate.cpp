// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Active-page navigation + per-page dirty tracking + external-edit envelope
// for SettingsController:
//   * setActivePage   — switch the viewed page (with parent→leaf redirect)
//   * onSettings/ExternalSettingsChanged — NOTIFY → dirty / reload hooks
//   * setNeedsSave / dirtyPages / isPageDirty — dirty-state surface for QML
//   * begin/endExternalEdit — stack envelope so sidebar/global widgets mark
//     the correct page dirty
//
// Split out of settingscontroller.cpp to keep that file under the 800-line
// cap. Same class, separate TU, no API change.

#include "settingscontroller.h"

#include "../core/logging.h"

#include <QDebug>

namespace PlasmaZones {

namespace {

// The two drag-to-reorder pages. Their state is the staged order optional
// (m_stagedSnappingOrder / m_stagedTilingOrder), not config-manifest keys, so
// per-page Reset/Discard dispatches to the ordering helpers rather than
// resetKeys/discardKeys.
bool isOrderingPage(const QString& page)
{
    return page == QLatin1String("snapping-ordering") || page == QLatin1String("tiling-ordering");
}

// Every animation leaf shares the single AnimationsPageController staging
// domain (there is no per-subpage granularity), so pageGroupChildren("animations")
// — the canonical leaf set — identifies them all. Discard reverts the whole tree.
bool isAnimationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("animations")).contains(page);
}

// The animation-tree config keys (Animations + Animations.WindowFiltering
// groups). revertPending() covers the per-event override FILES and the
// shader-tree dirty flag, but the caller contract (see revertPending()) says
// the in-memory Settings keys — shader tree, animation Profile blob, window
// filtering — are NOT reverted there; a follow-up must revert them, exactly
// what discardKeys() does. Kept together here as the animation "value" surface,
// paired with revertPending() for the full discard.
const Settings::ConfigKeyList& animationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::animationsGroup(), CD::backendKey()},
        {CD::animationsGroup(), CD::enabledKey()},
        {CD::animationsGroup(), CD::animationProfileKey()},
        {CD::animationsGroup(), CD::shaderProfileTreeKey()},
        {CD::animationsWindowFilteringGroup(), CD::transientWindowsKey()},
        {CD::animationsWindowFilteringGroup(), CD::notificationsAndOsdKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowWidthKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowHeightKey()},
    };
    return keys;
}

} // namespace

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

    // Key the anchor to the resolved leaf (mirroring setActivePage's redirect),
    // and only when the page is valid — otherwise a bogus address would latch the
    // anchor onto whatever page happened to be active.
    if (!anchor.isEmpty() && app() != nullptr) {
        const QString resolved = parentPageRedirects().value(page, page);
        if (validPageNames().contains(resolved)) {
            app()->setPendingAnchor(resolved, anchor);
        }
    }
}

void SettingsController::setActivePage(const QString& page)
{
    // Resolve parent category names (e.g. "snapping" → "snapping-overlay-behavior")
    const QString resolved = parentPageRedirects().value(page, page);

    if (!validPageNames().contains(resolved)) {
        qCWarning(PlasmaZones::lcCore) << "Unknown settings page:" << page;
        return;
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
    if (m_activePage != resolved) {
        // m_loading suppresses onSettingsPropertyChanged — the QML Loader
        // reacts synchronously to activePageChanged and new page creation
        // may trigger NOTIFY signals that would otherwise mark pages dirty.
        m_settingActivePage = true;
        m_loading = true;
        m_activePage = resolved;
        Q_EMIT activePageChanged();
        m_loading = false;
        m_settingActivePage = false;
    }
}

void SettingsController::onSettingsPropertyChanged()
{
    if (!m_saving && !m_loading) {
        setNeedsSave(true);
    }
}

void SettingsController::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
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
        const QString target = m_externalEditStack.isEmpty() ? m_activePage : m_externalEditStack.top();
        Q_ASSERT(!parentPageRedirects().contains(target));
        if (!m_dirtyPages.contains(target)) {
            m_dirtyPages.insert(target);
            Q_EMIT dirtyPagesChanged();
        }
    } else if (!m_dirtyPages.isEmpty()) {
        m_dirtyPages.clear();
        Q_EMIT dirtyPagesChanged();
    }
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

    // Ordering pages: dirty iff a custom order is staged that differs from the
    // saved order (a staged value equal to the saved order is not a change).
    if (page == QLatin1String("snapping-ordering")) {
        return m_stagedSnappingOrder.has_value() && *m_stagedSnappingOrder != m_settings.snappingLayoutOrder();
    }
    if (page == QLatin1String("tiling-ordering")) {
        return m_stagedTilingOrder.has_value() && *m_stagedTilingOrder != m_settings.tilingAlgorithmOrder();
    }

    // Animation pages share one staging domain, so any of them is dirty exactly
    // when the tree has unsaved edits. Value-based like the manifest pages, so
    // the badge and the kebab's Discard-enabled state stay correct on every
    // subpage. Two sources: the controller's file/shader-tree pending state
    // (hasPendingChanges) AND the plain animation Settings keys (profile,
    // shader tree value, window filtering) tracked against the baseline.
    if (isAnimationPage(page)) {
        if (m_animationsPage != nullptr && m_animationsPage->hasPendingChanges())
            return true;
        for (const auto& gk : animationConfigKeys()) {
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
    // animations) and virtual mid-level parents (animations-surfaces /
    // animations-library) share the same code path now. Recurse through
    // isPageDirty (not a bare m_dirtyPages lookup) so a manifest-backed child
    // contributes its value-based dirty state to the collapsed parent badge.
    const auto& groups = pageGroupChildren();
    const auto it = groups.constFind(page);
    if (it != groups.constEnd()) {
        for (const QString& child : *it) {
            if (isPageDirty(child))
                return true;
        }
    }
    return false;
}

bool SettingsController::pageSupportsReset(const QString& page) const
{
    // Config-manifest pages write schema defaults; ordering pages drop the
    // custom order. Animation pages have no reset-to-defaults path.
    return pageOwnedConfigKeys().contains(page) || isOrderingPage(page);
}

bool SettingsController::pageSupportsDiscard(const QString& page) const
{
    // Everything resettable can also discard, plus the animation pages (revert
    // via the shared staging domain).
    return pageSupportsReset(page) || isAnimationPage(page);
}

void SettingsController::reconcilePageDirty(const QString& page)
{
    // Match m_dirtyPages to the value-based truth for this manifest page.
    const bool dirty = isPageDirty(page);
    const bool had = m_dirtyPages.contains(page);
    if (dirty && !had) {
        m_dirtyPages.insert(page);
        Q_EMIT dirtyPagesChanged();
    } else if (!dirty && had) {
        m_dirtyPages.remove(page);
        Q_EMIT dirtyPagesChanged();
    }
}

void SettingsController::resetPage(const QString& page)
{
    // Ordering pages: "reset to defaults" means dropping the custom order.
    // resetSnappingOrder/resetTilingOrder stage the empty (default) order and
    // mark the active page dirty themselves.
    if (page == QLatin1String("snapping-ordering")) {
        resetSnappingOrder();
        return;
    }
    if (page == QLatin1String("tiling-ordering")) {
        resetTilingOrder();
        return;
    }

    const auto& manifest = pageOwnedConfigKeys();
    const auto it = manifest.constFind(page);
    if (it == manifest.constEnd()) {
        qCWarning(PlasmaZones::lcCore) << "resetPage: no config manifest for page" << page;
        return;
    }
    // Suppress onSettingsPropertyChanged for the reset's NOTIFY storm — it
    // would otherwise mark the ACTIVE page dirty (which may differ from the
    // page being reset). We reconcile `page`'s dirty state explicitly below.
    const bool wasLoading = m_loading;
    m_loading = true;
    m_settings.resetKeys(*it);
    m_loading = wasLoading;
    // Resetting to defaults usually diverges from the saved baseline, so the
    // page normally becomes dirty (stage → Save/Discard). If the defaults
    // already matched the baseline it stays clean — reconcile handles both.
    reconcilePageDirty(page);
}

void SettingsController::discardPage(const QString& page)
{
    const auto& manifest = pageOwnedConfigKeys();
    const auto it = manifest.constFind(page);
    if (it != manifest.constEnd()) {
        const bool wasLoading = m_loading;
        m_loading = true;
        m_settings.discardKeys(*it);
        m_loading = wasLoading;
        // Every owned key is back at the committed baseline, so the page is clean.
        reconcilePageDirty(page);
        return;
    }

    // Ordering pages: drop the staged custom order so the effective order falls
    // back to the saved value.
    if (page == QLatin1String("snapping-ordering") || page == QLatin1String("tiling-ordering")) {
        auto& staged = (page == QLatin1String("snapping-ordering")) ? m_stagedSnappingOrder : m_stagedTilingOrder;
        if (staged.has_value()) {
            staged.reset();
            if (page == QLatin1String("snapping-ordering"))
                Q_EMIT stagedSnappingOrderChanged();
            else
                Q_EMIT stagedTilingOrderChanged();
        }
        reconcilePageDirty(page);
        return;
    }

    // Animation pages share one staging domain — discard reverts the whole tree
    // in two halves. revertPending() restores the per-event override FILES and
    // clears the shader-tree dirty flag; discardKeys() reverts the in-memory
    // animation Settings keys (shader-tree value, Profile blob, window filtering)
    // the revertPending() contract explicitly leaves for a follow-up. Reverting
    // shaderProfileTree re-emits shaderProfileTreeChanged, which the controller
    // observes to refresh — the load()-equivalent that contract requires.
    if (isAnimationPage(page)) {
        if (m_animationsPage != nullptr) {
            m_animationsPage->revertPending();
        }
        const bool wasLoading = m_loading;
        m_loading = true;
        m_settings.discardKeys(animationConfigKeys());
        m_loading = wasLoading;
        // revertPending() left hasPendingChanges() false; clear every animation
        // leaf's m_dirtyPages marker so the global needsSave drops the entries
        // the pendingChangesChanged handler had attributed to the tree.
        bool changed = false;
        for (const QString& leaf : pageGroupChildren().value(QStringLiteral("animations"))) {
            if (m_dirtyPages.remove(leaf))
                changed = true;
        }
        if (changed) {
            Q_EMIT dirtyPagesChanged();
        }
        return;
    }

    // Parent category (e.g. "snapping" / "tiling" / "placement"): discard every
    // manifest-backed leaf beneath it. Lets the sidebar's section-disable
    // confirm drop a whole mode's staged edits before flipping the enable flag,
    // rather than relying on the framework PageAdapter::discard() no-op.
    const auto& groups = pageGroupChildren();
    const auto git = groups.constFind(page);
    if (git == groups.constEnd()) {
        qCWarning(PlasmaZones::lcCore) << "discardPage: no config manifest or child group for page" << page;
        return;
    }
    for (const QString& child : *git) {
        if (manifest.contains(child))
            discardPage(child);
    }
}

void SettingsController::beginExternalEdit(const QString& page)
{
    // Resolve parent categories to their canonical leaf — same rules as
    // setActivePage — so the sidebar can pass "snapping" or "tiling".
    const QString resolved = parentPageRedirects().value(page, page);
    if (!validPageNames().contains(resolved)) {
        qCWarning(PlasmaZones::lcCore) << "beginExternalEdit: unknown page" << page;
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
    m_externalEditStack.pop();
}

} // namespace PlasmaZones

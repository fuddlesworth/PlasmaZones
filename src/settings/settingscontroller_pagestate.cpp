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

// The two Quick Shortcuts pages. Their editable state is the per-mode staged
// quick-slot layout assignments in StagingService (daemon-backed); the shortcut
// keysequence is a read-only default in the standalone. Reset unassigns every
// slot (the default), Discard drops the staged edits.
bool isShortcutsPage(const QString& page)
{
    return page == QLatin1String("snapping-shortcuts") || page == QLatin1String("tiling-shortcuts");
}

// Quick-layout slots are numbered 1..9 (see SettingsController::getQuickLayoutSlot).
constexpr int kQuickLayoutSlotCount = 9;

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

// Every decoration leaf edits the single shared DecorationProfileTree settings
// key (one JSON blob covering windows + OSDs + popups), so
// pageGroupChildren("decoration") — the canonical leaf set — identifies them
// all and Reset/Discard act on the whole tree, mirroring the animation shared
// domain above.
bool isDecorationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("decorations")).contains(page);
}

// The decoration "value" surface: one Store-backed key. It cannot ride the
// pageOwnedConfigKeys manifest — all three decoration leaves would own the
// same key, violating the manifest's one-owner invariant — so the decoration
// branches in isPageDirty/resetPage/discardPage dispatch through this list
// instead. Unlike the animation domain there are no side files: reset/discard
// is entirely resetKeys/discardKeys, and the decorationProfileTreeChanged
// re-emit drives DecorationPageController::profilesChanged so open cards
// refresh.
const Settings::ConfigKeyList& decorationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::surfaceGroup(), CD::surfaceDecorationTreeKey()},
    };
    return keys;
}

// RAII suppression of onSettingsPropertyChanged around bulk resetKeys /
// discardKeys NOTIFY storms: raises the loading flag for the enclosing scope
// and restores the previous value on exit. Every reset/discard branch shares
// this instead of hand-rolling the save/set/qScopeGuard triple.
class LoadingScope
{
public:
    explicit LoadingScope(bool& flag)
        : m_flag(flag)
        , m_previous(flag)
    {
        flag = true;
    }
    ~LoadingScope()
    {
        m_flag = m_previous;
    }
    Q_DISABLE_COPY_MOVE(LoadingScope)

private:
    bool& m_flag;
    bool m_previous;
};

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

    // Quick Shortcuts pages: dirty iff a quick-slot edit is staged for the mode.
    if (page == QLatin1String("snapping-shortcuts")) {
        return m_staging.hasStagedSnappingQuickSlots();
    }
    if (page == QLatin1String("tiling-shortcuts")) {
        return m_staging.hasStagedTilingQuickSlots();
    }

    // Virtual Screens page: dirty iff any physical screen has a staged
    // virtual-screen edit (a split or a removal).
    if (page == QLatin1String("virtualscreens")) {
        return m_staging.hasStagedVirtualScreenConfigs();
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

    // Decoration pages share the single DecorationProfileTree key — value-based
    // like the manifest pages (see decorationConfigKeys for why the key can't
    // live in the manifest itself). All three leaves report the same state,
    // matching the shared-domain semantics: an edit on any decoration page is
    // an edit of the one tree.
    if (isDecorationPage(page)) {
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
    // Config-manifest pages write schema defaults (this includes the Windows
    // appearance page, whose Windows.* + Gaps.* keys are in the manifest); ordering
    // pages drop the custom order; shortcuts pages unassign every quick slot; the
    // virtual screens page unsplits every monitor; animation pages clear every
    // per-event override and reset the animation config keys; decoration pages
    // reset the shared DecorationProfileTree key.
    return pageOwnedConfigKeys().contains(page) || isOrderingPage(page) || isShortcutsPage(page)
        || page == QLatin1String("virtualscreens") || isAnimationPage(page) || isDecorationPage(page);
}

bool SettingsController::pageSupportsDiscard(const QString& page) const
{
    // Every page that supports reset also supports discard (animation pages are
    // already covered by pageSupportsReset). Kept as a distinct query so the two
    // kebab items can diverge if a future page becomes discard-only.
    return pageSupportsReset(page);
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
        const bool dirty = isPageDirty(page);
        const bool had = m_dirtyPages.contains(page);
        if (dirty && !had) {
            m_dirtyPages.insert(page);
            changed = true;
        } else if (!dirty && had) {
            m_dirtyPages.remove(page);
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT dirtyPagesChanged();
    }
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

void SettingsController::reconcileRuleBackedDirty()
{
    if (m_rulesPage == nullptr)
        return;
    // The Rules page stages user rules into the shared RuleController model;
    // attribute its dirty state from the value-based user-rules query so a user-rule
    // edit badges Rules even when the shared dirty bit does not transition. (The
    // Windows appearance page is a plain config page now — its dirtiness comes from
    // the config manifest via isPageDirty, not from the rule model.)
    bool changed = false;
    const auto sync = [this, &changed](const QString& page, bool dirty) {
        if (dirty) {
            if (!m_dirtyPages.contains(page)) {
                m_dirtyPages.insert(page);
                changed = true;
            }
        } else if (m_dirtyPages.remove(page)) {
            changed = true;
        }
    };
    sync(QStringLiteral("rules"), m_rulesPage->userRulesDirty());
    if (changed) {
        Q_EMIT dirtyPagesChanged();
    }
}

void SettingsController::resetPage(const QString& page)
{
    // Animation pages (whole tree, shared domain): reset to defaults =
    // clear every per-event override file AND reset the animation config keys
    // (Profile, ShaderProfileTree, WindowFiltering, Enabled, Backend) to their
    // schema defaults. Both are STAGED like ordinary animation edits — the
    // cleared files are snapshotted, so a subsequent Discard restores them, and
    // Save commits. User presets / motion-set libraries are left alone (like
    // user rules on the Rules page). Suppress onSettingsPropertyChanged during
    // the config reset; mark the active page dirty explicitly below.
    if (isAnimationPage(page)) {
        if (m_animationsPage != nullptr) {
            const LoadingScope loadingScope(m_loading);
            m_animationsPage->clearAllOverrides();
            m_settings.resetKeys(animationConfigKeys());
        }
        // isPageDirty(animation) is value-based (hasPendingChanges || any
        // animation key modified). Reconcile EVERY animation leaf, not just the
        // active page: the shared domain means an edit made while a sibling
        // subpage was active left ITS m_dirtyPages entry behind, and a reset
        // that lands back on the committed baseline must clear those too or
        // needsSave() sticks true with no badge to explain it. Batched so
        // dirtyPagesChanged fires at most once (like the discard path).
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        return;
    }

    // Decoration pages (whole tree, shared domain): reset the single
    // DecorationProfileTree key to its schema default — the empty/neutral tree
    // (borders are rule-owned; the tree carries only opt-in shader-pack
    // decoration). Staged like ordinary edits: Save commits, Discard restores
    // the baseline. Same NOTIFY-storm suppression as the manifest path.
    if (isDecorationPage(page)) {
        {
            const LoadingScope loadingScope(m_loading);
            m_settings.resetKeys(decorationConfigKeys());
        }
        // isPageDirty(decoration) is value-based. Reconcile EVERY decoration
        // leaf, not just the active page: the shared key means an edit made
        // while a sibling leaf was active left ITS m_dirtyPages entry behind,
        // and a reset that lands back on the committed baseline must clear
        // those too or needsSave() sticks true with no badge to explain it
        // (mirrors the discardPage decoration branch). Batched so
        // dirtyPagesChanged fires at most once.
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("decorations")));
        return;
    }

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

    // Quick Shortcuts: "reset to defaults" unassigns every slot (the default is
    // no assignment). Stage an empty id ONLY for slots that currently hold an
    // assignment — an already-empty slot needs no change, so resetting an
    // all-default page stages nothing (stays clean) and Save flushes no no-op
    // clears. quickLayoutSlotsChanged refreshes the slot cards.
    if (isShortcutsPage(page)) {
        const bool snapping = (page == QLatin1String("snapping-shortcuts"));
        for (int slot = 1; slot <= kQuickLayoutSlotCount; ++slot) {
            const QString current = snapping ? getQuickLayoutSlot(slot) : getTilingQuickLayoutSlot(slot);
            if (current.isEmpty())
                continue;
            if (snapping)
                m_staging.stageSnappingQuickSlot(slot, QString());
            else
                m_staging.stageTilingQuickSlot(slot, QString());
        }
        Q_EMIT quickLayoutSlotsChanged();
        reconcilePageDirty(page);
        return;
    }

    // Virtual Screens: "reset to defaults" unsplits every monitor. Drop any
    // in-progress split edits, then stage a removal for each physical screen
    // that currently HAS virtual screens (a >1-entry config). save() flushes
    // the removals to the daemon + Settings. dirtyPagesChanged (emitted below)
    // drives the page's _refreshConfig so the editor re-reads the reverted state.
    if (page == QLatin1String("virtualscreens")) {
        m_staging.clearVirtualScreenConfigs();
        const QVariantList physicalScreens = screens();
        for (const QVariant& entry : physicalScreens) {
            const QString name = entry.toMap().value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            const QString physId = physicalScreenId(name);
            // A >1-entry config means the physical screen is split into virtual
            // screens; single/empty means it is already at the native default.
            if (!physId.isEmpty() && getVirtualScreenConfig(physId).size() > 1)
                m_staging.stageVirtualScreenRemoval(physId);
        }
        if (m_staging.hasStagedVirtualScreenConfigs())
            m_dirtyPages.insert(page);
        else
            m_dirtyPages.remove(page);
        Q_EMIT dirtyPagesChanged();
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
    {
        const LoadingScope loadingScope(m_loading);
        m_settings.resetKeys(*it);
    }
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
        {
            const LoadingScope loadingScope(m_loading);
            m_settings.discardKeys(*it);
        }
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

    // Quick Shortcuts: drop the mode's staged quick-slot edits so the getters
    // fall back to the daemon's saved slots.
    if (isShortcutsPage(page)) {
        const bool snapping = (page == QLatin1String("snapping-shortcuts"));
        const bool hadStaged =
            snapping ? m_staging.hasStagedSnappingQuickSlots() : m_staging.hasStagedTilingQuickSlots();
        if (hadStaged) {
            if (snapping)
                m_staging.clearSnappingQuickSlots();
            else
                m_staging.clearTilingQuickSlots();
            Q_EMIT quickLayoutSlotsChanged();
        }
        reconcilePageDirty(page);
        return;
    }

    // Virtual Screens: drop every staged virtual-screen edit so the editor
    // falls back to the daemon's saved configs. The Discard menu item is only
    // enabled while staged edits exist, so there is always something to clear.
    if (page == QLatin1String("virtualscreens")) {
        m_staging.clearVirtualScreenConfigs();
        m_dirtyPages.remove(page);
        // Always emit so the page's dirtyPagesChanged handler re-reads the
        // reverted config, even if other pages keep the global flag dirty.
        Q_EMIT dirtyPagesChanged();
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
        {
            const LoadingScope loadingScope(m_loading);
            m_settings.discardKeys(animationConfigKeys());
        }
        // revertPending() left hasPendingChanges() false; reconcile every
        // animation leaf against the value-based truth (all clean post-revert)
        // so the global needsSave drops the entries the pendingChangesChanged
        // handler had attributed to the tree. Batched: one emission at most.
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        return;
    }

    // Decoration pages share the single DecorationProfileTree key — discard
    // reverts it to the committed baseline. discardKeys re-emits
    // decorationProfileTreeChanged, which DecorationPageController forwards as
    // profilesChanged so the open cards refresh. Clear every decoration leaf's
    // stale m_dirtyPages marker: the shared key means one leaf's edit may have
    // badged the others.
    if (isDecorationPage(page)) {
        {
            const LoadingScope loadingScope(m_loading);
            m_settings.discardKeys(decorationConfigKeys());
        }
        // Reconcile every decoration leaf against the value-based truth (all
        // clean post-discard). Batched: one emission at most.
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("decorations")));
        return;
    }

    // Parent category (e.g. "snapping" / "tiling" / "placement"): discard every
    // discardable leaf beneath it. Lets the sidebar's section-disable confirm
    // drop a whole mode's staged edits before flipping the enable flag, rather
    // than relying on the framework PageAdapter::discard() no-op.
    const auto& groups = pageGroupChildren();
    const auto git = groups.constFind(page);
    if (git == groups.constEnd()) {
        qCWarning(PlasmaZones::lcCore) << "discardPage: no config manifest or child group for page" << page;
        return;
    }
    // Dispatch on pageSupportsDiscard, not manifest membership: the ordering and
    // Quick Shortcuts children are deliberately absent from the config manifest
    // (they revert through their own staged machinery) yet isPageDirty recurses
    // into them, so a parent is reported dirty when only they are edited. A
    // manifest-only walk would leave those staged edits in place, and the
    // section-disable confirm would then apply a partial edit — exactly what that
    // confirm exists to prevent. Non-discardable children (e.g. shaders browser)
    // return false and are skipped.
    for (const QString& child : *git) {
        if (pageSupportsDiscard(child))
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

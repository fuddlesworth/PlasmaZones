// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-page Reset and Discard for SettingsController:
//   * pageSupportsReset / pageSupportsDiscard — which kebab items to offer
//   * resetPage    — write schema defaults for the page's owned state
//   * discardPage  — drop the page's staged edits back to the baseline
//
// Split out of settingscontroller_pagestate.cpp when that file passed the
// 1150-line ceiling. The two halves divide cleanly by job: _pagestate.cpp
// answers "what is dirty and which page am I on", this file answers "put this
// one page back". The page-class predicates and shared-domain key lists both
// halves need live in settingscontroller_pagekeys.h.
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

namespace PlasmaZones {

namespace {

// Reset/Discard-only helpers. These live at file scope rather than in the
// shared settingscontroller_pagekeys.h because nothing outside this TU uses
// them — the dirty-tracking half never did, so no translation-unit boundary is
// crossed and internal linkage is the right default.

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

// Raises the loading flag for the enclosing scope and restores the PREVIOUS
// value on exit (not a hard clear), so nesting is safe. Suppresses
// onSettingsPropertyChanged so a reset/discard's own writes do not mark the
// page dirty again. Every branch below shares this instead of hand-rolling the
// save/set/qScopeGuard triple.
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

bool SettingsController::pageSupportsReset(const QString& page) const
{
    // Config-manifest pages write schema defaults (this includes the Windows
    // appearance page, whose Windows.* + Gaps.* keys are in the manifest); ordering
    // pages drop the custom order; shortcuts pages unassign every quick slot; the
    // virtual screens page unsplits every monitor; animation pages clear every
    // per-event override and reset the animation config keys; decoration surface
    // pages clear their own root subtree of the DecorationProfileTree (the
    // sets/shaders leaves the whole key).
    //
    // The condensed simple pages delegate to their backing advanced pages,
    // which resets those pages' FULL key sets — deliberately wider than the
    // subset the simple page displays. Narrowing is not expressible: the
    // per-algorithm tuning the tiling simple page edits lives in ONE blob key
    // (Tiling.Algorithm/perAlgorithmSettings) that also carries master count
    // and custom script params. "Reset this page" in simple mode therefore
    // means "reset this whole feature area", which is the honest reading of a
    // page that IS the feature area in that mode.
    return pageOwnedConfigKeys().contains(page) || simplePageBackingPages().contains(page) || isOrderingPage(page)
        || isShortcutsPage(page) || page == QLatin1String("virtualscreens") || isAnimationPage(page)
        || isDecorationPage(page);
}

bool SettingsController::pageSupportsDiscard(const QString& page) const
{
    // Every page that supports reset also supports discard (animation pages are
    // already covered by pageSupportsReset). Kept as a distinct query so the two
    // kebab items can diverge if a future page becomes discard-only.
    return pageSupportsReset(page);
}

void SettingsController::resetPage(const QString& page)
{
    // Manifest-owned pages FIRST, mirroring isPageDirty and discardPage. A page
    // can belong to a shared-domain GROUP (animations / decorations) yet own its
    // own config keys: window-appearance is a decorations-group child, so
    // isDecorationPage(page) is true, but its Windows.* / Gaps.* keys live in
    // the manifest. Checking the shared-domain branches first would reset the
    // whole DecorationProfileTree instead of the page's own keys, and since
    // isPageDirty and discardPage both route window-appearance through its
    // manifest, Reset must too or the three disagree.
    {
        const auto& manifest = pageOwnedConfigKeys();
        const auto ownedIt = manifest.constFind(page);
        if (ownedIt != manifest.constEnd()) {
            // Suppress onSettingsPropertyChanged for the reset's NOTIFY storm;
            // reconcile `page`'s dirty state explicitly below.
            {
                const LoadingScope loadingScope(m_loading);
                m_settings.resetKeys(*ownedIt);
            }
            reconcilePageDirty(page);
            return;
        }
    }

    // Condensed SimpleOnly pages own no keys — Reset delegates to each
    // backing advanced page in turn (each recursion reconciles itself, and
    // reconcilePageDirty cascades back to this page).
    {
        const auto& backing = simplePageBackingPages();
        const auto backingIt = backing.constFind(page);
        if (backingIt != backing.constEnd()) {
            const DirtyEmitScope batch(*this);
            for (const QString& backingPage : *backingIt)
                resetPage(backingPage);
            return;
        }
    }

    // Animation pages: reset to defaults, scoped like the decoration domain
    // below. A SURFACE leaf clears only its own event subtree — its per-event
    // override FILES and its shader-tree overrides — leaving the other surfaces,
    // General's config keys, and the library untouched. General resets only its
    // config keys (enable / motion Profile / filtering). A library leaf resets
    // the whole tree (every file + every animation key). All staged like ordinary
    // edits: cleared files are snapshotted so Discard restores them, and Save
    // commits. Suppress onSettingsPropertyChanged during the reset; reconcile the
    // whole animation leaf set below (a scoped reset can flip a sibling's badge,
    // and any stale m_dirtyPages entry must clear too).
    if (isAnimationPage(page)) {
        const AnimationPageScope scope = animationPageScope(page);
        {
            const LoadingScope loadingScope(m_loading);
            if (scope.kind == AnimationPageScope::ConfigOnly) {
                m_settings.resetKeys(animationGeneralConfigKeys());
            } else if (scope.kind == AnimationPageScope::EventSubtree) {
                // Files first. -1 means the clear did not complete (refused
                // mid-discard, or a file could not be removed): leave the shader
                // tree un-reset so the page stays visibly dirty for a retry rather
                // than reporting a half-done reset as clean. The page toasts why.
                if (m_animationsPage != nullptr
                    && m_animationsPage->clearOverridesUnder(animationScopedBuiltInPaths(scope)) < 0) {
                    reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
                    return;
                }
                // Shader tree: clear only this scope's overrides (default = none).
                PhosphorAnimationShaders::ShaderProfileTree tree = m_settings.shaderProfileTree();
                bool changed = false;
                const QStringList overridden = tree.overriddenPaths();
                for (const QString& path : overridden) {
                    if (animationPathInScope(path, scope) && tree.clearOverride(path))
                        changed = true;
                }
                if (changed)
                    m_settings.setShaderProfileTree(tree);
                // A page hosting the global timing / filter cards resets those
                // keys too (the condensed simple page). Deliberately the
                // General key list, NOT animationConfigKeys: the shader-tree
                // key is per-event state already handled per scope above.
                if (scope.includeGeneralKeys)
                    m_settings.resetKeys(animationGeneralConfigKeys());
            } else {
                // WholeTree library leaf: files + every animation key.
                if (m_animationsPage != nullptr && m_animationsPage->clearAllOverrides() < 0) {
                    reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
                    return;
                }
                m_settings.resetKeys(animationConfigKeys());
            }
        }
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        return;
    }

    // Decoration pages: reset to the built-in defaults. The stored blob holds
    // only user edits; the built-in card chrome for the OSD and popups is a
    // read-side seed layer (Settings::decorationProfileTree overlays
    // ConfigDefaults::decorationProfileTree at lowest precedence). So a SURFACE
    // page clears only its own root subtree's overrides — the seeds show
    // through again, restoring the default chrome for seeded surfaces and "no
    // decoration" everywhere else — leaving the other two roots (and the
    // global baseline) standing; a non-surface leaf (sets/shaders, empty root)
    // resets the whole tree key.
    // Staged like ordinary edits: Save commits, Discard restores the baseline.
    // Same NOTIFY-storm suppression as the manifest path.
    if (isDecorationPage(page)) {
        {
            const LoadingScope loadingScope(m_loading);
            const QString root = decorationSurfaceRoot(page);
            if (root.isEmpty()) {
                m_settings.resetKeys(decorationConfigKeys());
            } else {
                PhosphorSurfaceShaders::DecorationProfileTree tree = m_settings.decorationProfileTree();
                bool changed = false;
                // overriddenPaths() returns a copy, so clearing during the walk
                // is safe. Only this root's overrides go; the stored default
                // for a surface subtree is "no user overrides", and the write
                // path strips whatever seed-injected entries the merged read
                // view carried, so the seeds re-surface on the next read.
                const QStringList paths = tree.overriddenPaths();
                for (const QString& path : paths) {
                    if (decorationPathInRoot(path, root) && tree.clearOverride(path))
                        changed = true;
                }
                if (changed)
                    m_settings.setDecorationProfileTree(tree);
            }
        }
        // isPageDirty(decoration) is value-based. Reconcile EVERY decoration
        // leaf, not just the active page: a subtree reset can flip a sibling
        // parent/child leaf's badge, and any stale m_dirtyPages entry left by an
        // edit made while another leaf was active must clear too or needsSave()
        // sticks true with no badge to explain it (mirrors the discardPage
        // decoration branch). Batched so dirtyPagesChanged fires at most once.
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
        bool staged = false;
        for (int slot = 1; slot <= kQuickLayoutSlotCount; ++slot) {
            const QString current = snapping ? getQuickLayoutSlot(slot) : getTilingQuickLayoutSlot(slot);
            if (current.isEmpty())
                continue;
            if (snapping)
                m_staging.stageSnappingQuickSlot(slot, QString());
            else
                m_staging.stageTilingQuickSlot(slot, QString());
            staged = true;
        }
        // Only when something actually changed: the comment above already says
        // an all-default page stages nothing, so firing the NOTIFY regardless
        // would contradict it and re-run every bound slot card for no edit.
        if (staged)
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
        // Unconditional emit, deliberately against the emit-on-change rule (as
        // setPendingAnchor is, for the same kind of reason). The dirty-set
        // membership may well be unchanged, but clearVirtualScreenConfigs() +
        // the restaging above always change the STAGED state, and this page has
        // no NOTIFY of its own — dirtyPagesChanged is what drives its
        // _refreshConfig re-read. Gating on the membership flip would leave the
        // editor showing the pre-reset split whenever the page was already
        // dirty. If this page ever gains its own refresh signal, move the
        // re-read onto it and gate this emit.
        emitDirtyPagesChanged();
        return;
    }

    // Manifest-owned pages and the condensed simple pages were handled at the
    // top of this function; anything reaching here matched no manifest entry,
    // no backing-page delegation, and no shared-domain / ordering / shortcut /
    // virtual-screen branch, so there is nothing to reset.
    qCWarning(PlasmaZones::lcCore) << "resetPage: no config manifest for page" << page;
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

    // Condensed SimpleOnly pages own no keys — Discard delegates to each
    // backing advanced page (mirrors resetPage; reconcilePageDirty cascades
    // back to this page).
    {
        const auto& backing = simplePageBackingPages();
        const auto backingIt = backing.constFind(page);
        if (backingIt != backing.constEnd()) {
            const DirtyEmitScope batch(*this);
            for (const QString& backingPage : *backingIt)
                discardPage(backingPage);
            return;
        }
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
        emitDirtyPagesChanged();
        return;
    }

    // Animation pages: discard reverts to the committed baseline, scoped like the
    // decoration domain below. A SURFACE leaf reverts only its own event subtree —
    // its override FILES (revertPendingUnder) and its shader-tree paths (restored
    // to baseline) — so discarding OSDs cannot drop a pending Windows edit.
    // General reverts only its config keys. A library leaf reverts the whole tree
    // (all files via revertPending + every animation key via discardKeys).
    // Reverting the shader tree re-emits shaderProfileTreeChanged, which the
    // controller observes to refresh the cards.
    if (isAnimationPage(page)) {
        const AnimationPageScope scope = animationPageScope(page);
        if (scope.kind == AnimationPageScope::ConfigOnly) {
            {
                const LoadingScope loadingScope(m_loading);
                m_settings.discardKeys(animationGeneralConfigKeys());
            }
        } else if (scope.kind == AnimationPageScope::EventSubtree) {
            if (m_animationsPage != nullptr)
                m_animationsPage->revertPendingUnder(animationScopedBuiltInPaths(scope));
            // Shader tree: restore only this scope's paths to their baseline value
            // (re-add / change / remove), leaving the other surfaces' staged edits.
            const LoadingScope loadingScope(m_loading);
            PhosphorAnimationShaders::ShaderProfileTree current = m_settings.shaderProfileTree();
            const PhosphorAnimationShaders::ShaderProfileTree baseline = m_settings.committedShaderProfileTree();
            QSet<QString> paths;
            for (const QString& p : current.overriddenPaths())
                if (animationPathInScope(p, scope))
                    paths.insert(p);
            for (const QString& p : baseline.overriddenPaths())
                if (animationPathInScope(p, scope))
                    paths.insert(p);
            bool changed = false;
            for (const QString& path : paths) {
                if (baseline.hasOverride(path)) {
                    const auto committed = baseline.directOverride(path);
                    if (!current.hasOverride(path) || current.directOverride(path) != committed) {
                        current.setOverride(path, committed);
                        changed = true;
                    }
                } else if (current.clearOverride(path)) {
                    changed = true;
                }
            }
            if (changed)
                m_settings.setShaderProfileTree(current);
            // A page hosting the global timing / filter cards discards those
            // keys too (the condensed simple page), mirroring its reset scope.
            if (scope.includeGeneralKeys)
                m_settings.discardKeys(animationGeneralConfigKeys());
        } else {
            // WholeTree library leaf.
            if (m_animationsPage != nullptr)
                m_animationsPage->revertPending();
            const LoadingScope loadingScope(m_loading);
            m_settings.discardKeys(animationConfigKeys());
        }
        // Reconcile every animation leaf against the value-based truth (this
        // surface clean post-discard; siblings unchanged). Value-based on purpose:
        // revertPendingUnder can refuse mid-async or retain a failed restore.
        // Batched: one emission at most.
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        return;
    }

    // Decoration pages: discard reverts to the committed baseline. A SURFACE page
    // reverts only its own root subtree — each such path is restored to the
    // baseline's value (re-added, changed, or removed) while the other two roots'
    // staged edits stand — so discarding OSDs cannot drop a pending Windows edit.
    // A non-surface leaf (sets/shaders, empty root) reverts the whole tree key via
    // discardKeys. Either way the setDecorationProfileTree / discardKeys write
    // re-emits decorationProfileTreeChanged, which DecorationPageController
    // forwards as profilesChanged so the open cards refresh.
    if (isDecorationPage(page)) {
        {
            const LoadingScope loadingScope(m_loading);
            const QString root = decorationSurfaceRoot(page);
            if (root.isEmpty()) {
                m_settings.discardKeys(decorationConfigKeys());
            } else {
                PhosphorSurfaceShaders::DecorationProfileTree current = m_settings.decorationProfileTree();
                const PhosphorSurfaceShaders::DecorationProfileTree baseline =
                    m_settings.committedDecorationProfileTree();
                // Union of this root's overridden paths across both trees so a
                // path that only exists in one (a staged add, or a baseline
                // override the user cleared) is reconciled in the right direction.
                QSet<QString> paths;
                for (const QString& p : current.overriddenPaths())
                    if (decorationPathInRoot(p, root))
                        paths.insert(p);
                for (const QString& p : baseline.overriddenPaths())
                    if (decorationPathInRoot(p, root))
                        paths.insert(p);
                bool changed = false;
                for (const QString& path : paths) {
                    if (baseline.hasOverride(path)) {
                        const auto committed = baseline.directOverride(path);
                        if (!current.hasOverride(path) || current.directOverride(path) != committed) {
                            current.setOverride(path, committed);
                            changed = true;
                        }
                    } else if (current.clearOverride(path)) {
                        changed = true;
                    }
                }
                if (changed)
                    m_settings.setDecorationProfileTree(current);
            }
        }
        // Reconcile every decoration leaf against the value-based truth (this
        // surface clean post-discard; siblings unchanged). Batched: one emission
        // at most.
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
    // return false and are skipped, as are the condensed simple pages — they
    // own no keys and would only re-discard their backing pages, which are
    // always siblings in this same set.
    const DirtyEmitScope batch(*this);
    for (const QString& child : *git) {
        if (pageSupportsDiscard(child) && !simplePageBackingPages().contains(child))
            discardPage(child);
    }
}

} // namespace PlasmaZones

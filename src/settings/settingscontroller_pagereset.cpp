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
#include "dbusutils.h"
#include "settingscontroller_pagekeys.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QDebug>
#include <QSet>
#include <QStringList>
#include <QVariant>

namespace PlasmaZones {

namespace {

// Reset/Discard-only helpers: nothing outside this TU uses them, so internal
// linkage is the right default. (orderingPageKind / isShortcutsPage / ScopedFlag
// are NOT here: they live in settingscontroller_pagekeys.h so the dirty check,
// the reset and the discard read one definition.)

// The animation shader tree and the decoration profile tree are unrelated types
// with the same override vocabulary (overriddenPaths / hasOverride /
// directOverride / setOverride / clearOverride), and Reset and Discard walk both
// the same way. The two walks are templates over the tree type plus an in-scope
// predicate so a surface-scoping rule can never be right on one domain and wrong
// on the other.

// Reset: clear every override whose path is in scope. Returns whether anything
// went, i.e. whether the caller must write the tree back.
template<typename Tree, typename InScope>
bool clearOverridesInScope(Tree& tree, InScope&& inScope)
{
    // overriddenPaths() returns a copy, so clearing during the walk is safe.
    bool changed = false;
    const QStringList paths = tree.overriddenPaths();
    for (const QString& path : paths) {
        if (inScope(path) && tree.clearOverride(path)) {
            changed = true;
        }
    }
    return changed;
}

// Discard: restore every in-scope path of `current` to its baseline value —
// re-added, changed, or cleared. Walks the UNION of both trees' overridden paths
// so a path present in only one (a staged add, or a baseline override the user
// cleared) is reconciled in the right direction. Out-of-scope paths keep their
// staged edits. Returns whether anything changed.
template<typename Tree, typename InScope>
bool restoreScopeToBaseline(Tree& current, const Tree& baseline, InScope&& inScope)
{
    QSet<QString> paths;
    for (const QString& p : current.overriddenPaths()) {
        if (inScope(p)) {
            paths.insert(p);
        }
    }
    for (const QString& p : baseline.overriddenPaths()) {
        if (inScope(p)) {
            paths.insert(p);
        }
    }
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
    return changed;
}

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
    //
    // Parent categories too, PLUS the leaves — mirroring pageSupportsDiscard.
    // Reset is invoked with activeDirtyScope (Main.qml / ConfirmDialogs), which
    // hoists a condensed page to its group, so "snapping" / "tiling" arrive here
    // and resetPage has a parent-category branch that resets the group's
    // resettable leaves. Without this the kebab's Reset item would hide in
    // simple mode (the scope is a group id no leaf branch accepts) and the badge
    // the group carries could never be cleared by Reset.
    return pageOwnedConfigKeys().contains(page) || simplePageBackingPages().contains(page)
        || orderingPageKind(page) != OrderingPageKind::None || isShortcutsPage(page)
        || page == QLatin1String("virtualscreens") || isAnimationPage(page) || isDecorationPage(page)
        || pageGroupChildren().contains(page);
}

bool SettingsController::pageSupportsDiscard(const QString& page) const
{
    // Discard accepts exactly what Reset does: every resettable leaf PLUS the
    // parent categories. Both resetPage and discardPage now have a
    // parent-category branch (the sidebar's section-disable confirm and the
    // simple-mode kebab pass "snapping" / "tiling" / "animations" via
    // activeDirtyScope), and pageSupportsReset already returns true for every
    // pageGroupChildren() key, so the two predicates are identical. Kept as its
    // own function for call-site clarity and so a leaf that ever becomes
    // discardable-but-not-resettable has an obvious place to diverge.
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
    //
    // The WHOLE branch order below matches isPageDirty and discardPage, not just
    // this first test: manifest, backing pages, ordering, shortcuts, virtual
    // screens, animation, decoration, parent-category group. Any page matching two branches must reach
    // the same one in all three, so the three are written to be diffed against
    // each other. Add a branch to one, add it in the same position in the others.
    {
        const auto& manifest = pageOwnedConfigKeys();
        const auto ownedIt = manifest.constFind(page);
        if (ownedIt != manifest.constEnd()) {
            // Suppress onSettingsPropertyChanged for the reset's NOTIFY storm;
            // reconcile `page`'s dirty state explicitly below.
            {
                const ScopedFlag loadingScope(m_loading);
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
            for (const QString& backingPage : *backingIt) {
                // Release-build half of the acyclic invariant asserted at
                // simplePageBackingPages(): a backing page that is itself a
                // backing key would recurse back into this branch forever, so
                // skip it rather than overflow the stack.
                if (backing.contains(backingPage))
                    continue;
                resetPage(backingPage);
            }
            return;
        }
    }

    // Ordering pages: "reset to defaults" means dropping the custom order.
    // resetSnappingOrder/resetTilingOrder stage the empty (default) order and
    // mark the active page dirty themselves.
    switch (orderingPageKind(page)) {
    case OrderingPageKind::Snapping:
        resetSnappingOrder();
        return;
    case OrderingPageKind::Tiling:
        resetTilingOrder();
        return;
    case OrderingPageKind::None:
        break;
    }

    // Quick Shortcuts: "reset to defaults" unassigns every slot (the default is
    // no assignment). Stage an empty id ONLY for slots that currently hold an
    // assignment — an already-empty slot needs no change, so resetting an
    // all-default page stages nothing (stays clean) and Save flushes no no-op
    // clears. quickLayoutSlotsChanged refreshes the slot cards.
    if (isShortcutsPage(page)) {
        const bool snapping = (page == QLatin1String("snapping-shortcuts"));
        // Fetch the whole map in ONE D-Bus call. The per-slot accessors fall
        // through to the daemon for any slot with no staged value, so the
        // loop below used to block the GUI thread on up to nine sequential
        // round-trips — and an all-default page paid all nine to stage nothing.
        const QDBusMessage slotsReply =
            DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                   QStringLiteral("getAllQuickLayoutSlots"), {snapping ? 0 : 1});
        if (slotsReply.type() != QDBusMessage::ReplyMessage) {
            // Every per-slot accessor this replaced guards the reply type. An
            // error map is indistinguishable from "all slots already
            // unassigned", so falling through would stage nothing, reconcile
            // the page CLEAN, and look exactly like a successful reset of a
            // page that had no assignments. Refuse instead, and say so: this
            // is the same contract clearAllOverrides() uses for a daemon it
            // cannot reach.
            qCWarning(PlasmaZones::lcCore)
                << "resetPage: could not read quick layout slots from the daemon:" << slotsReply.errorMessage();
            // Reconcile before leaving so a pre-existing stale dirty entry for
            // this page is cleaned on this exit too, matching every other path.
            reconcilePageDirty(page);
            Q_EMIT pageResetFailed(page, QString(ReasonDaemonUnreachable));
            return;
        }
        const QVariantMap allSlots = slotsReply.arguments().value(0).toMap();
        bool staged = false;
        for (int slot = 1; slot <= QUICK_LAYOUT_SLOT_COUNT; ++slot) {
            // Staged value wins over the daemon's, matching the per-slot
            // accessors' precedence.
            QString current;
            const bool haveStaged = snapping ? m_staging.stagedSnappingQuickSlot(slot, current)
                                             : m_staging.stagedTilingQuickSlot(slot, current);
            if (!haveStaged)
                current = allSlots.value(QString::number(slot)).toString();
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
        // Which screens are split comes from the persisted config, NOT from a
        // getVirtualScreenConfig() per screen. That loop was one blocking D-Bus
        // round-trip per monitor on the GUI thread, and worse, it could not
        // report a failure: the accessor returns an empty list both for "the
        // daemon did not answer" and for "this monitor is not split", so an
        // unreachable daemon staged zero removals and the page then reported
        // itself clean — a failed reset indistinguishable from a successful one.
        // The Settings map is local, is the same state save() writes back, and
        // has no failure mode to confuse with an empty result.
        const QHash<QString, PhosphorScreens::VirtualScreenConfig> configs = m_settings.virtualScreenConfigs();
        for (auto it = configs.cbegin(); it != configs.cend(); ++it) {
            if (!it.key().isEmpty() && it.value().hasSubdivisions())
                m_staging.stageVirtualScreenRemoval(it.key());
        }
        syncDirtyMembership(page, m_staging.hasStagedVirtualScreenConfigs());
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
        // Set when a file clear does not complete. The reconcile and the
        // pageResetFailed emit below both run AFTER the ScopedFlag closes on
        // every path, success or failure — reconciling while m_loading is still
        // raised skips maybeDrainPendingExternalReload, so the two paths used to
        // differ for no reason.
        bool failed = false;
        {
            const ScopedFlag loadingScope(m_loading);
            if (scope.kind == AnimationPageScope::ConfigOnly) {
                m_settings.resetKeys(animationGeneralConfigKeys());
            } else if (scope.kind == AnimationPageScope::EventSubtree) {
                // Files first. -1 means the clear did not complete (refused
                // mid-discard, or a file could not be removed): leave the shader
                // tree and the General keys un-reset so the page stays visibly
                // dirty for a retry rather than reporting a half-done reset as
                // clean, and tell the user why via pageResetFailed below.
                if (m_animationsPage != nullptr
                    && m_animationsPage->clearOverridesUnder(animationScopedBuiltInPaths(scope)) < 0) {
                    failed = true;
                }
                if (!failed) {
                    // Shader tree: clear only this scope's overrides (the stored
                    // default for an event subtree is "no overrides").
                    PhosphorAnimationShaders::ShaderProfileTree tree = m_settings.shaderProfileTree();
                    if (clearOverridesInScope(tree, [&scope](const QString& path) {
                            return animationPathInScope(path, scope);
                        })) {
                        m_settings.setShaderProfileTree(tree);
                    }
                    // A page hosting the global timing / filter cards resets those
                    // keys too (the condensed simple page). Deliberately the
                    // General key list, NOT animationConfigKeys: the shader-tree
                    // key is per-event state already handled per scope above.
                    if (scope.includeGeneralKeys) {
                        m_settings.resetKeys(animationGeneralConfigKeys());
                    }
                }
            } else {
                // WholeTree library leaf: files + every animation key.
                if (m_animationsPage != nullptr && m_animationsPage->clearAllOverrides() < 0) {
                    failed = true;
                } else {
                    m_settings.resetKeys(animationConfigKeys());
                }
            }
        }
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        if (failed) {
            // Same contract as the Quick Shortcuts branch: a reset that changed
            // nothing is indistinguishable from a successful reset of an
            // already-default page, so it must say so. Without this the user
            // presses Reset, the page stays dirty, and the only trace is a
            // qCWarning inside AnimationsPageController.
            Q_EMIT pageResetFailed(page, QString(ReasonOverridesNotCleared));
        }
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
            const ScopedFlag loadingScope(m_loading);
            const QString root = decorationSurfaceRoot(page);
            if (root.isEmpty()) {
                m_settings.resetKeys(decorationConfigKeys());
            } else {
                // Only this root's overrides go; the stored default for a surface
                // subtree is "no user overrides", and the write path strips
                // whatever seed-injected entries the merged read view carried, so
                // the seeds re-surface on the next read.
                PhosphorSurfaceShaders::DecorationProfileTree tree = m_settings.decorationProfileTree();
                if (clearOverridesInScope(tree, [&root](const QString& path) {
                        return decorationPathInRoot(path, root);
                    })) {
                    m_settings.setDecorationProfileTree(tree);
                }
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

    // Parent category (e.g. "snapping" / "tiling"): reset every resettable leaf
    // beneath it, mirroring discardPage's parent-category branch. Reset is
    // invoked with activeDirtyScope, which in advanced mode equals activePage (a
    // leaf, handled above) but in simple mode hoists a condensed page to its
    // group — so "snapping" / "tiling" / "animations" arrive here and must reset
    // the hidden advanced siblings the rail badge aggregates, or Reset visibly
    // no-ops against a still-badged group. Condensed simple children are skipped:
    // they own no keys and would only re-reset their backing pages, always
    // siblings already in this same set (the guard discardPage uses too). A child
    // that is itself a group is skipped for the same release-build reason the
    // backing-page branch skips a backing key: pageGroupChildren() values are leaf
    // ids only today, but a future nested group would otherwise recurse forever.
    const auto& groups = pageGroupChildren();
    const auto git = groups.constFind(page);
    if (git != groups.constEnd()) {
        const DirtyEmitScope batch(*this);
        for (const QString& child : *git) {
            if (pageSupportsReset(child) && !simplePageBackingPages().contains(child) && !groups.contains(child))
                resetPage(child);
        }
        return;
    }

    // Anything reaching here matched no manifest entry, no backing-page
    // delegation, no ordering / shortcut / virtual-screen / shared-domain
    // branch, and no child group, so there is nothing to reset.
    qCWarning(PlasmaZones::lcCore) << "resetPage: no config manifest or child group for page" << page;
}

void SettingsController::discardPage(const QString& page)
{
    const auto& manifest = pageOwnedConfigKeys();
    const auto it = manifest.constFind(page);
    if (it != manifest.constEnd()) {
        {
            const ScopedFlag loadingScope(m_loading);
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
            for (const QString& backingPage : *backingIt) {
                // Same acyclic guard as resetPage's delegation above.
                if (backing.contains(backingPage))
                    continue;
                discardPage(backingPage);
            }
            return;
        }
    }

    // Ordering pages: drop the staged custom order so the effective order falls
    // back to the saved value.
    switch (orderingPageKind(page)) {
    case OrderingPageKind::Snapping:
        if (m_stagedSnappingOrder.has_value()) {
            m_stagedSnappingOrder.reset();
            Q_EMIT stagedSnappingOrderChanged();
        }
        reconcilePageDirty(page);
        return;
    case OrderingPageKind::Tiling:
        if (m_stagedTilingOrder.has_value()) {
            m_stagedTilingOrder.reset();
            Q_EMIT stagedTilingOrderChanged();
        }
        reconcilePageDirty(page);
        return;
    case OrderingPageKind::None:
        break;
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
        syncDirtyMembership(page, false);
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
        // ONE scope over the whole chain, matching resetPage. Every branch
        // needs it for the same reason (the revert provokes settings NOTIFYs,
        // and without suppression they reach onSettingsPropertyChanged and
        // re-dirty the active page in the middle of the discard that is
        // clearing it), and the two functions are written to be diffed against
        // each other — three separate scopes made a reader prove the forms
        // equivalent instead of see it.
        //
        // The explicit block is load-bearing: the scope MUST close before the
        // reconcile below, which reads the dirty state the discard just
        // produced. Letting it run to the end of the `if` would leave
        // suppression up across the reconcile.
        // Set when a revert refuses OUTRIGHT (returns false) and no async
        // discard worker owns the snapshot map — i.e. a genuine failure a retry
        // could fix, not the benign "the global async discard already took the
        // restore" case revertPending()/revertPendingUnder() document. Mirrors
        // resetPage's `failed`/pageResetFailed pairing so a refused Discard is
        // not silent (the value-based reconcile leaves the page badged, and
        // without a word the user reads that as "Discard did nothing").
        bool failed = false;
        {
            const ScopedFlag loadingScope(m_loading);
            if (scope.kind == AnimationPageScope::ConfigOnly) {
                m_settings.discardKeys(animationGeneralConfigKeys());
            } else if (scope.kind == AnimationPageScope::EventSubtree) {
                if (m_animationsPage != nullptr
                    && !m_animationsPage->revertPendingUnder(animationScopedBuiltInPaths(scope))
                    && !m_animationsPage->asyncRevertInFlight()) {
                    failed = true;
                }
                // Shader tree: restore only this scope's paths to their baseline value
                // (re-add / change / remove), leaving the other surfaces' staged
                // edits. Covered by the scope opened above.
                PhosphorAnimationShaders::ShaderProfileTree current = m_settings.shaderProfileTree();
                const PhosphorAnimationShaders::ShaderProfileTree baseline = m_settings.committedShaderProfileTree();
                if (restoreScopeToBaseline(current, baseline, [&scope](const QString& path) {
                        return animationPathInScope(path, scope);
                    })) {
                    m_settings.setShaderProfileTree(current);
                }
                // A page hosting the global timing / filter cards discards those
                // keys too (the condensed simple page), mirroring its reset scope.
                if (scope.includeGeneralKeys)
                    m_settings.discardKeys(animationGeneralConfigKeys());
            } else {
                // WholeTree library leaf.
                if (m_animationsPage != nullptr && !m_animationsPage->revertPending()
                    && !m_animationsPage->asyncRevertInFlight()) {
                    failed = true;
                }
                m_settings.discardKeys(animationConfigKeys());
            }
        }

        // Reconcile every animation leaf against the value-based truth (this
        // surface clean post-discard; siblings unchanged). Value-based on purpose:
        // revertPendingUnder can refuse mid-async or retain a failed restore.
        // Batched: one emission at most.
        reconcilePagesDirty(pageGroupChildren().value(QStringLiteral("animations")));
        if (failed) {
            Q_EMIT pageDiscardFailed(page, QString(ReasonOverridesNotCleared));
        }
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
            const ScopedFlag loadingScope(m_loading);
            const QString root = decorationSurfaceRoot(page);
            if (root.isEmpty()) {
                m_settings.discardKeys(decorationConfigKeys());
            } else {
                PhosphorSurfaceShaders::DecorationProfileTree current = m_settings.decorationProfileTree();
                const PhosphorSurfaceShaders::DecorationProfileTree baseline =
                    m_settings.committedDecorationProfileTree();
                if (restoreScopeToBaseline(current, baseline, [&root](const QString& path) {
                        return decorationPathInRoot(path, root);
                    })) {
                    m_settings.setDecorationProfileTree(current);
                }
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
    // always siblings in this same set. A child that is itself a group is
    // skipped for the same release-build reason the backing-page branch skips a
    // backing key: values are leaf ids today, but a future nested group would
    // otherwise recurse forever.
    const DirtyEmitScope batch(*this);
    for (const QString& child : *git) {
        if (pageSupportsDiscard(child) && !simplePageBackingPages().contains(child) && !groups.contains(child))
            discardPage(child);
    }
}

} // namespace PlasmaZones

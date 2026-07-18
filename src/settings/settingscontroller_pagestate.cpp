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
// Same class as settingscontroller.cpp, separate TU, no API change.

#include "settingscontroller.h"

#include "../core/logging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorSurface/DecorationProfileTree.h>

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

// Every animation leaf shares the single AnimationsPageController staging domain
// AND the single ShaderProfileTree key, but Reset/Discard/dirty are NOT
// whole-tree: each surface leaf (windows/osds/overlays/desktops/motion/dragging/
// panels/widgets/editor) owns one event-path subtree (see animationPageScope),
// the general leaf owns only the config keys, and the sets/shaders library leaves
// act on the whole editable tree. Scoping keeps a Reset on one surface from
// wiping the others (mirrors the decoration domain below).
bool isAnimationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("animations")).contains(page);
}

// The animation config keys OTHER than the per-event surfaces: the global
// enable, the baseline motion Profile blob, and window filtering. Owned by the
// Animations → General leaf. The ShaderProfileTree key is deliberately ABSENT —
// it is per-event state scoped through animationPageScope, not a General-page
// key. (The per-event override FILES are likewise not config keys.)
const Settings::ConfigKeyList& animationGeneralConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::animationsGroup(), CD::enabledKey()},
        {CD::animationsGroup(), CD::animationProfileKey()},
        {CD::animationsWindowFilteringGroup(), CD::transientWindowsKey()},
        {CD::animationsWindowFilteringGroup(), CD::notificationsAndOsdKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowWidthKey()},
        {CD::animationsWindowFilteringGroup(), CD::minimumWindowHeightKey()},
    };
    return keys;
}

// The WHOLE animation "value" surface — General's keys PLUS the ShaderProfileTree
// key. Used only by the non-surface library leaves (sets/shaders), whose
// Reset/Discard act on the entire editable tree (paired with clearAllOverrides /
// revertPending for the per-event FILES).
const Settings::ConfigKeyList& animationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys = []() {
        Settings::ConfigKeyList k = animationGeneralConfigKeys();
        k.append({CD::animationsGroup(), CD::shaderProfileTreeKey()});
        return k;
    }();
    return keys;
}

// The per-page scope of an animation leaf. A surface leaf owns one event-path
// root subtree (some with a carve-out); General owns only config keys; the
// library leaves own the whole tree.
struct AnimationPageScope
{
    enum Kind {
        EventSubtree,
        ConfigOnly,
        WholeTree
    };
    Kind kind = WholeTree;
    // For EventSubtree: a path is in scope iff it is under an `include` root AND
    // not under an `exclude` root. window.movement (Motion) EXCLUDES
    // window.movement.move, which the Dragging page owns — the one carve-out.
    QStringList include;
    QStringList exclude;
};

AnimationPageScope animationPageScope(const QString& page)
{
    if (page == QLatin1String("animations-general"))
        return {AnimationPageScope::ConfigOnly, {}, {}};
    // Roots mirror the surfacePath prefixes the QML pages bind and the
    // ProfilePaths taxonomy. Keep in lockstep with the *Page.qml event lists.
    static const QHash<QString, QPair<QStringList, QStringList>> kEventRoots{
        {QStringLiteral("animations-windows"), {{QStringLiteral("window.appearance")}, {}}},
        {QStringLiteral("animations-window-motion"),
         {{QStringLiteral("window.movement")}, {QStringLiteral("window.movement.move")}}},
        {QStringLiteral("animations-window-dragging"), {{QStringLiteral("window.movement.move")}, {}}},
        {QStringLiteral("animations-osds"), {{QStringLiteral("osd")}, {}}},
        {QStringLiteral("animations-overlays"), {{QStringLiteral("popup")}, {}}},
        {QStringLiteral("animations-desktops"), {{QStringLiteral("desktop")}, {}}},
        {QStringLiteral("animations-side-panels"), {{QStringLiteral("panel")}, {}}},
        {QStringLiteral("animations-widgets"), {{QStringLiteral("widget")}, {}}},
        {QStringLiteral("animations-editor"), {{QStringLiteral("editor")}, {}}},
    };
    const auto it = kEventRoots.constFind(page);
    if (it != kEventRoots.cend())
        return {AnimationPageScope::EventSubtree, it->first, it->second};
    // animations-presets / animations-motionsets / animations-shaders.
    return {AnimationPageScope::WholeTree, {}, {}};
}

// True iff @p path is @p root or a descendant of it, for any root in @p roots.
bool animationPathUnderAny(const QString& path, const QStringList& roots)
{
    for (const QString& root : roots) {
        if (path == root || path.startsWith(root + QLatin1Char('.')))
            return true;
    }
    return false;
}

// True iff @p path falls inside an EventSubtree scope (under an include root,
// clear of every exclude root).
bool animationPathInScope(const QString& path, const AnimationPageScope& scope)
{
    return animationPathUnderAny(path, scope.include) && !animationPathUnderAny(path, scope.exclude);
}

// Built-in event paths that fall inside @p scope — the file-backed paths a
// surface leaf's Reset/Discard/dirty acts on.
QStringList animationScopedBuiltInPaths(const AnimationPageScope& scope)
{
    QStringList out;
    for (const QString& path : PhosphorAnimation::ProfilePaths::allBuiltInPaths()) {
        if (animationPathInScope(path, scope))
            out.append(path);
    }
    return out;
}

// True iff the two shader trees' overrides differ anywhere inside @p scope. Walks
// the union of both trees' overridden paths (so an override present in one and
// absent in the other counts) filtered to the scope — this also catches
// plugin-added paths that allBuiltInPaths() would miss.
bool shaderTreeScopeDiffers(const PhosphorAnimationShaders::ShaderProfileTree& current,
                            const PhosphorAnimationShaders::ShaderProfileTree& baseline,
                            const AnimationPageScope& scope)
{
    QSet<QString> paths;
    for (const QString& p : current.overriddenPaths())
        if (animationPathInScope(p, scope))
            paths.insert(p);
    for (const QString& p : baseline.overriddenPaths())
        if (animationPathInScope(p, scope))
            paths.insert(p);
    for (const QString& p : paths) {
        if (current.hasOverride(p) != baseline.hasOverride(p))
            return true;
        if (current.directOverride(p) != baseline.directOverride(p))
            return true;
    }
    return false;
}

// Every decoration leaf reads/writes the single shared DecorationProfileTree
// settings key (one JSON blob covering windows + OSDs + popups), so
// pageGroupChildren("decorations") — the canonical leaf set — identifies them
// all. Reset/Discard/dirty are NOT whole-tree, though: the three surface pages
// each own one root subtree (see decorationSurfaceRoot), so resetting OSDs must
// not touch the Windows overrides. Only the sets/shaders library leaves act on
// the whole editable tree.
bool isDecorationPage(const QString& page)
{
    return SettingsController::pageGroupChildren().value(QStringLiteral("decorations")).contains(page);
}

// The DecorationProfileTree root a decoration surface page owns. The three
// surface pages each edit exactly one root subtree — "window" (+ .tiled/.snapped/
// .floating), "osd", or "popup" (+ .snapAssist/.zoneSelector/.layoutPicker) — so
// per-page Reset/Discard/dirty must be scoped to that root or one surface's
// revert clobbers the others (the bug this mapping fixes). The roots mirror the
// surfacePath prefixes the QML pages bind (DecorationWindowsPage etc.) and the
// DecorationSupportedPaths taxonomy. Returns an empty string for the non-surface
// decoration leaves — the sets library and the read-only shaders browser — whose
// Reset/Discard/dirty act on the whole editable tree (every root at once).
QString decorationSurfaceRoot(const QString& page)
{
    static const QHash<QString, QString> roots{
        {QStringLiteral("decorations-windows"), QStringLiteral("window")},
        {QStringLiteral("decorations-osds"), QStringLiteral("osd")},
        {QStringLiteral("decorations-popups"), QStringLiteral("popup")},
    };
    return roots.value(page);
}

// True iff @p path is @p root itself or a descendant of it ("window" owns
// "window", "window.tiled", …; "osd" owns only "osd"). The dot guard keeps a
// sibling root with a shared prefix from leaking in — there are none today, but
// it costs nothing and documents the intent.
bool decorationPathInRoot(const QString& path, const QString& root)
{
    return path == root || path.startsWith(root + QLatin1Char('.'));
}

// True iff the two trees' direct overrides differ anywhere under @p root. The
// baseline (path "") is never under a surface root, so it is correctly excluded
// — no surface page edits the global baseline (decoration sets carry none
// either; see decorationpagecontroller_sets.cpp). Walks the union of both trees'
// overridden paths so an override PRESENT in one and ABSENT in the other counts.
bool decorationRootDiffers(const PhosphorSurfaceShaders::DecorationProfileTree& current,
                           const PhosphorSurfaceShaders::DecorationProfileTree& baseline, const QString& root)
{
    QSet<QString> paths;
    for (const QString& p : current.overriddenPaths())
        if (decorationPathInRoot(p, root))
            paths.insert(p);
    for (const QString& p : baseline.overriddenPaths())
        if (decorationPathInRoot(p, root))
            paths.insert(p);
    for (const QString& p : paths) {
        if (current.hasOverride(p) != baseline.hasOverride(p))
            return true;
        if (current.directOverride(p) != baseline.directOverride(p))
            return true;
    }
    return false;
}

// The decoration "value" surface: one Store-backed key. It cannot ride the
// pageOwnedConfigKeys manifest — every decoration leaf would own the
// same key, violating the manifest's one-owner invariant — so the decoration
// branches in isPageDirty/resetPage/discardPage dispatch through this list
// instead. Unlike the animation domain there are no side files: reset/discard
// is entirely resetKeys/discardKeys, and the decorationProfileTreeChanged
// re-emit drives DecorationPageController::profilesChanged so open cards
// refresh.
//
// The Decorations.WindowFiltering knobs are NOT here — they live on the
// Decorations → General (window-appearance) page and ride that page's
// pageOwnedConfigKeys manifest entry, not this shared surface-tree domain.
const Settings::ConfigKeyList& decorationConfigKeys()
{
    using CD = ConfigDefaults;
    static const Settings::ConfigKeyList keys{
        {CD::decorationsGroup(), CD::decorationProfileTreeKey()},
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
        // The page name arrives over D-Bus (SettingsAppAdaptor::setActivePage), so it is
        // caller-supplied and unbounded. A typo still deserves a trace, so keep one — at
        // debug rather than warning, which is what kept the log-flooding concern honest.
        // The NAME is echoed: a trace that cannot say which page was wrong carries none of
        // the information a typo trace exists for, and debug is off in production anyway.
        // Same treatment as the adaptor's unknown-key trace (settingsadaptor.cpp).
        qCDebug(PlasmaZones::lcCore) << "Unknown settings page requested:" << page;
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
    // edits; the user resolves the divergence by saving over the external
    // change or discarding.
    if (needsSave()) {
        qCInfo(lcCore) << "External settings change ignored: unsaved local edits take precedence";
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
        const QString target = m_externalEditStack.isEmpty() ? m_activePage : m_externalEditStack.top();
        // The target must resolve to a concrete leaf page; a parent-category id
        // would poison m_dirtyPages with a page the user never directly edits.
        // Assert in debug, and in release skip the insert rather than dirtying a
        // redirect target.
        Q_ASSERT(!parentPageRedirects().contains(target));
        if (parentPageRedirects().contains(target)) {
            return;
        }
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
            return shaderTreeScopeDiffers(m_settings.shaderProfileTree(), m_settings.committedShaderProfileTree(),
                                          scope);
        }
        // WholeTree library leaves (sets / shaders).
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
    // per-event override and reset the animation config keys; decoration surface
    // pages clear their own root subtree of the DecorationProfileTree (the
    // sets/shaders leaves the whole key).
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

    // Decoration pages: reset to the schema default (the empty/neutral tree —
    // borders are rule-owned, the tree carries only opt-in shader-pack
    // decoration). A SURFACE page clears only its own root subtree's overrides,
    // leaving the other two roots (and the global baseline) standing; a
    // non-surface leaf (sets/shaders, empty root) resets the whole tree key.
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
                // is safe. Only this root's overrides go; the default for a
                // surface subtree is "no overrides".
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

    // Manifest-owned pages were handled at the top of this function; anything
    // reaching here matched no manifest entry and no shared-domain / ordering /
    // shortcut / virtual-screen branch, so there is nothing to reset.
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

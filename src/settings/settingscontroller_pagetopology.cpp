// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Static sidebar parent/child topology accessors for SettingsController:
//   * parentPageRedirects()  — virtual-parent id → first-leaf id (D-Bus / CLI
//     / Q_INVOKABLE page selection).
//   * pageGroupChildren()    — parent id → set of leaf child ids (dirtiness
//     propagation up collapsed categories).
//   * pageOwnedConfigKeys()  — leaf id → (group, key) manifest (per-page
//     Reset / Discard).
//   * validPageNames()       — the set of navigable leaf page ids.
//
// Split out of settingscontroller_pageregistration.cpp to keep that file under
// the 800-line cap (see CLAUDE.md). All methods here are members of
// PlasmaZones::SettingsController — same class, separate translation unit, no
// API change.

#include "settingscontroller.h"

#include "../config/configdefaults.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

const QHash<QString, QString>& SettingsController::parentPageRedirects()
{
    // Parent sidebar categories have no QML component — resolve them to their
    // first child so D-Bus / CLI / Q_INVOKABLE callers get a sensible result.
    // Includes both top-level parents AND mid-level virtual parents
    // (`animations-transitions`, `animations-motion`, `animations-library`) so
    // any of those names passed via `--page` or D-Bus lands on a real leaf
    // instead of triggering the generic "Unknown settings page" warning.
    static const QHash<QString, QString> redirects{
        {QStringLiteral("display"), QStringLiteral("virtualscreens")},
        // "appearance" is the inline-collapsible parent of the Animations and
        // Decoration trees; land on its first child's first leaf.
        {QStringLiteral("appearance"), QStringLiteral("animations-general")},
        // "placement" is the inline-collapsible parent of snapping/tiling; it
        // has no page of its own, so a --page=placement / D-Bus call lands on
        // the first leaf of its first child (snapping → snapping-overlay-behavior).
        {QStringLiteral("placement"), QStringLiteral("snapping-overlay-behavior")},
        {QStringLiteral("snapping"), QStringLiteral("snapping-overlay-behavior")},
        // Tiling's first child is the Window category → its first leaf is Behavior.
        {QStringLiteral("tiling"), QStringLiteral("tiling-behavior")},
        {QStringLiteral("animations"), QStringLiteral("animations-general")},
        {QStringLiteral("animations-transitions"), QStringLiteral("animations-windows")},
        {QStringLiteral("animations-motion"), QStringLiteral("animations-window-motion")},
        {QStringLiteral("animations-library"), QStringLiteral("animations-presets")},
        {QStringLiteral("decorations"), QStringLiteral("window-appearance")},
        {QStringLiteral("decorations-surfaces"), QStringLiteral("decorations-windows")},
        {QStringLiteral("decorations-library"), QStringLiteral("decorations-sets")},
        // The "rules" parent virtual retired when Rules promoted
        // to a top-level entry; no redirect needed because there is no
        // longer a parent id to land on.
        // The *-cat virtual headers (registered as collapsible category
        // entries in buildApplicationController() in the sibling
        // _pageregistration.cpp) are real entries
        // in the framework PageRegistry — without an explicit redirect,
        // anything that drives setCurrentPageId("snapping-overlay-cat")
        // would land on a page id that isn't in validPageNames() and
        // m_activePage would diverge from m_app->currentPageId(). Map
        // each *-cat to its first leaf so the active-page state stays
        // coherent regardless of how the id was reached.
        {QStringLiteral("snapping-overlay-cat"), QStringLiteral("snapping-overlay-behavior")},
        {QStringLiteral("snapping-config-cat"), QStringLiteral("snapping-ordering")},
        {QStringLiteral("tiling-config-cat"), QStringLiteral("tiling-ordering")},
    };
    return redirects;
}

const QHash<QString, QSet<QString>>& SettingsController::pageGroupChildren()
{
    // Single source of truth: parent name → set of leaf child page
    // names. Used by `isPageDirty` to propagate dirty state from a
    // leaf to any group it belongs to. Covers parents at every level:
    // top-level categories (placement / display / animations) AND the
    // mid-level virtual parents nested beneath them (snapping / tiling
    // under placement; animations-transitions / animations-motion /
    // animations-library; the *-cat headers) whose children don't share
    // their name prefix — the
    // explicit set sidesteps the asymmetry between prefix-walk and
    // direct membership lookup.
    //
    // The "animations" entry is built at static-init by unioning the
    // virtual sub-buckets (`animations-transitions`, `animations-motion`,
    // `animations-library`) with the leaf that hangs directly off
    // `animations` (general).
    // Without this, a future leaf added to a virtual parent only would
    // silently miss the top-level dirty propagation.
    //
    // Keep the per-group leaf lists in sync with the parentId arguments
    // in `buildApplicationController()` in the sibling _pageregistration.cpp —
    // that function is the
    // registry's source of truth for the page tree; this static map
    // exists because `isPageDirty()` is a hot path and the per-call walk
    // over `m_app->pageRegistry()->allPages()` to derive the parent→leaf
    // mapping would otherwise re-scan every page on every dirty-check.
    // (The historical "_childItems" reference in Main.qml is obsolete —
    // the chrome now consumes registry topology directly via Sidebar.qml.)
    static const QSet<QString> kAnimationsTransitionsChildren{
        QStringLiteral("animations-windows"), QStringLiteral("animations-osds"), QStringLiteral("animations-overlays"),
        QStringLiteral("animations-desktops")};
    static const QSet<QString> kAnimationsMotionChildren{
        QStringLiteral("animations-window-motion"), QStringLiteral("animations-side-panels"),
        QStringLiteral("animations-widgets"), QStringLiteral("animations-editor")};
    static const QSet<QString> kAnimationsLibraryChildren{QStringLiteral("animations-presets"),
                                                          QStringLiteral("animations-motionsets"),
                                                          QStringLiteral("animations-shaders")};
    static const QSet<QString> kAnimationsDirectChildren{QStringLiteral("animations-general")};
    static const QSet<QString> kAnimationsAllLeaves = kAnimationsDirectChildren + kAnimationsTransitionsChildren
        + kAnimationsMotionChildren + kAnimationsLibraryChildren;
    // Decoration drill-down — Surfaces / Library sub-buckets, mirroring
    // animations. decorations-shaders is a read-only browser; like
    // animations-shaders it still rides the shared decoration domain (see
    // isDecorationPage), so it reports the tree's dirty state and its kebab
    // Reset/Discard act on the whole tree — the shared-domain semantics, not a
    // per-page edit surface.
    static const QSet<QString> kDecorationSurfacesChildren{
        QStringLiteral("decorations-windows"),
        QStringLiteral("decorations-osds"),
        QStringLiteral("decorations-popups"),
    };
    static const QSet<QString> kDecorationLibraryChildren{QStringLiteral("decorations-sets"),
                                                          QStringLiteral("decorations-shaders")};
    // Decoration → General is the window-appearance page (its historical id).
    static const QSet<QString> kDecorationDirectChildren{QStringLiteral("window-appearance")};
    static const QSet<QString> kDecorationAllLeaves =
        kDecorationDirectChildren + kDecorationSurfacesChildren + kDecorationLibraryChildren;
    // Mid-level *-cat collapsible category headers under the snapping /
    // tiling drill-down parents. Sidebar.qml renders these as collapsible
    // section headers; when COLLAPSED the `sidebar.trailingDelegate` in
    // Main.qml calls isPageDirty(<*-cat>) to decide whether to light the
    // badge. Without these entries that lookup would always
    // return false even when a leaf inside the collapsed section is
    // dirty (mirrors the snapping/tiling parent entries above, just one
    // level deeper). Keep in sync with the regVirtual *-cat registrations
    // in buildApplicationController() in the sibling _pageregistration.cpp.
    static const QSet<QString> kSnappingOverlayChildren{
        QStringLiteral("snapping-overlay-behavior"),
        QStringLiteral("snapping-overlay-appearance"),
    };
    // Zone Selector and Window are standalone top leaves under "snapping" (no
    // category split) — folded directly into the parent sets below. The window
    // border / title-bar appearance moved to the shared top-level Window
    // Appearance page, so Snapping → Window is just the Behavior leaf now.
    static const QString kSnappingZoneSelector = QStringLiteral("snapping-zoneselector");
    static const QString kSnappingWindowBehavior = QStringLiteral("snapping-window-behavior");
    static const QSet<QString> kSnappingConfigChildren{
        QStringLiteral("snapping-ordering"),
        QStringLiteral("snapping-shortcuts"),
        QStringLiteral("snapping-shaders"),
    };
    static const QSet<QString> kSnappingAllLeaves = kSnappingOverlayChildren
        + QSet<QString>{kSnappingZoneSelector, kSnappingWindowBehavior} + kSnappingConfigChildren;
    // Window (Behavior) and Algorithm are standalone top leaves under "tiling"
    // (no category), so they fold directly into the tiling/placement parent
    // sets below. The window border / title-bar appearance moved to the shared
    // top-level Window Appearance page.
    static const QString kTilingBehavior = QStringLiteral("tiling-behavior");
    static const QString kTilingAlgorithm = QStringLiteral("tiling-algorithm");
    static const QSet<QString> kTilingConfigChildren{
        QStringLiteral("tiling-ordering"),
        QStringLiteral("tiling-shortcuts"),
    };
    static const QSet<QString> kTilingAllLeaves =
        QSet<QString>{kTilingBehavior, kTilingAlgorithm} + kTilingConfigChildren;
    static const QHash<QString, QSet<QString>> groups{
        {QStringLiteral("snapping"), kSnappingAllLeaves},
        {QStringLiteral("tiling"), kTilingAllLeaves},
        // "placement" is the inline-collapsible parent of snapping + tiling;
        // when collapsed its dirty badge must light if any snapping OR tiling
        // leaf is dirty, so its leaf set is the union of both modes' leaves.
        {QStringLiteral("placement"), kSnappingAllLeaves + kTilingAllLeaves},
        {QStringLiteral("snapping-overlay-cat"), kSnappingOverlayChildren},
        {QStringLiteral("snapping-config-cat"), kSnappingConfigChildren},
        {QStringLiteral("tiling-config-cat"), kTilingConfigChildren},
        {QStringLiteral("animations"), kAnimationsAllLeaves},
        {QStringLiteral("animations-transitions"), kAnimationsTransitionsChildren},
        {QStringLiteral("animations-motion"), kAnimationsMotionChildren},
        {QStringLiteral("decorations-surfaces"), kDecorationSurfacesChildren},
        {QStringLiteral("decorations-library"), kDecorationLibraryChildren},
        {QStringLiteral("animations-library"), kAnimationsLibraryChildren},
        // "appearance" wraps the Animations and Decoration trees (the window-
        // appearance page rides kDecorationAllLeaves as Decoration → General);
        // its collapsed badge lights if any of them is dirty.
        {QStringLiteral("appearance"), kAnimationsAllLeaves + kDecorationAllLeaves},
        {QStringLiteral("decorations"), kDecorationAllLeaves},
        // Top-level inline-collapsible parents must also propagate
        // dirty state from their leaves — without these entries the
        // sidebar's collapsed dirty badge stays cold even when a
        // child page is dirty. Mirrors the registry topology in
        // buildApplicationController() in the sibling _pageregistration.cpp.
        {QStringLiteral("display"), {QStringLiteral("virtualscreens"), QStringLiteral("layouts")}},
        // No "rules" entry — Rules is a top-level leaf so its
        // dirty state propagates without a parent-bucket intermediary.
    };
    return groups;
}

const QHash<QString, Settings::ConfigKeyList>& SettingsController::pageOwnedConfigKeys()
{
    // Per-page config-key manifest driving the breadcrumb kebab's Reset /
    // Discard actions. Each leaf lists the (group, key) pairs it edits; a
    // per-page Reset writes each key's schema default and a per-page Discard
    // reverts each to the committed baseline (see Settings::resetKeys /
    // discardKeys). ALWAYS express keys through ConfigDefaults accessors — never
    // inline literals (CLAUDE.md). The pairs mirror the store-backed getters in
    // settings.cpp.
    //
    // INVARIANTS (maintained by hand — a unit guard would have to link the whole
    // SettingsController TU): every pair must be a schema-declared key, and no
    // key may be owned by two pages (a shared key would let one page's Discard
    // revert another page's edit). When adding a page, copy the (group, key)
    // accessors verbatim from that page's getter in settings.cpp.
    //
    // Scope: KConfig-backed settings pages. The Rules page (separate rule store),
    // the layouts page (separate-store), the controller-mediated ordering/shortcuts
    // pages, the Animations tree, and the Decoration pages (whose three leaves
    // SHARE the one DecorationProfileTree key — the one-owner invariant above
    // forbids listing a shared key here) are deliberately absent because they
    // revert through their own machinery (the special-case branches in
    // reset/discardPage), not because Reset/Discard is unsupported —
    // pageSupportsReset returns true for everything except the read/browse pages
    // with no revertible config state. The Windows appearance page IS
    // config-backed (Windows.* + Gaps.*), so it lists its owned keys here.
    using CD = ConfigDefaults;
    static const QHash<QString, Settings::ConfigKeyList> manifest{
        {QStringLiteral("general"),
         {
             {CD::renderingGroup(), CD::backendKey()},
             // Shader Effects moved here from snapping-overlay-appearance: frame
             // rate + audio spectrum drive EVERY shader category (overlay,
             // animation, surface decoration), not just snapping overlays.
             {CD::shadersGroup(), CD::enabledKey()},
             {CD::shadersGroup(), CD::frameRateKey()},
             {CD::shadersGroup(), CD::audioVisualizerKey()},
             {CD::shadersGroup(), CD::audioSpectrumBarCountKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::suppressDefaultLayoutAssignmentKey()},
             {CD::exclusionsGroup(), CD::transientWindowsKey()},
             {CD::exclusionsGroup(), CD::minimumWindowWidthKey()},
             {CD::exclusionsGroup(), CD::minimumWindowHeightKey()},
         }},
        {QStringLiteral("snapping-overlay-behavior"),
         {
             {CD::snappingBehaviorGroup(), CD::toggleActivationKey()},
             {CD::snappingBehaviorZoneSpanGroup(), CD::enabledKey()},
             {CD::snappingBehaviorZoneSpanGroup(), CD::toggleActivationKey()},
             {CD::snappingGapsGroup(), CD::adjacentThresholdKey()},
             {CD::snappingBehaviorDisplayGroup(), CD::showOnAllMonitorsKey()},
             {CD::snappingBehaviorDisplayGroup(), CD::filterByAspectRatioKey()},
         }},
        {QStringLiteral("snapping-overlay-appearance"),
         {
             {CD::snappingZonesColorsGroup(), CD::useSystemKey()},
             {CD::snappingZonesColorsGroup(), CD::highlightKey()},
             {CD::snappingZonesColorsGroup(), CD::inactiveKey()},
             {CD::snappingZonesColorsGroup(), CD::borderKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontColorKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontFamilyKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontSizeScaleKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontWeightKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontItalicKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontUnderlineKey()},
             {CD::snappingZonesLabelsGroup(), CD::fontStrikeoutKey()},
             {CD::snappingZonesOpacityGroup(), CD::activeKey()},
             {CD::snappingZonesOpacityGroup(), CD::inactiveKey()},
             {CD::snappingZonesBorderGroup(), CD::widthKey()},
             {CD::snappingZonesBorderGroup(), CD::radiusKey()},
             {CD::snappingEffectsGroup(), CD::blurKey()},
             {CD::snappingEffectsGroup(), CD::showNumbersKey()},
             {CD::snappingEffectsGroup(), CD::flashOnSwitchKey()},
         }},
        {QStringLiteral("snapping-zoneselector"),
         {
             {CD::snappingZoneSelectorGroup(), CD::enabledKey()},
             {CD::snappingZoneSelectorGroup(), CD::triggerDistanceKey()},
             {CD::snappingZoneSelectorGroup(), CD::positionKey()},
             {CD::snappingZoneSelectorGroup(), CD::layoutModeKey()},
             {CD::snappingZoneSelectorGroup(), CD::sizeModeKey()},
             {CD::snappingZoneSelectorGroup(), CD::gridColumnsKey()},
             {CD::snappingZoneSelectorGroup(), CD::maxRowsKey()},
             {CD::snappingZoneSelectorGroup(), CD::previewWidthKey()},
             {CD::snappingZoneSelectorGroup(), CD::previewHeightKey()},
         }},
        {QStringLiteral("snapping-window-behavior"),
         {
             {CD::snappingBehaviorSnapAssistGroup(), CD::featureEnabledKey()},
             {CD::snappingBehaviorSnapAssistGroup(), CD::enabledKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::keepOnResolutionChangeKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::moveNewToLastZoneKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::autoAssignAllLayoutsKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::restoreOnUnsnapKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::restoreOnLoginKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::restoreFloatedOnLoginKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::unfloatFallbackToZoneKey()},
             {CD::snappingBehaviorWindowHandlingGroup(), CD::stickyWindowHandlingKey()},
             {CD::snappingBehaviorGroup(), CD::focusNewWindowsKey()},
             {CD::snappingBehaviorGroup(), CD::focusFollowsMouseKey()},
         }},
        {QStringLiteral("tiling-behavior"),
         {
             {CD::tilingBehaviorGroup(), CD::toggleActivationKey()},
             {CD::tilingBehaviorGroup(), CD::insertPositionKey()},
             {CD::tilingBehaviorGroup(), CD::respectMinimumSizeKey()},
             {CD::tilingBehaviorGroup(), CD::stickyWindowHandlingKey()},
             {CD::tilingBehaviorGroup(), CD::dragBehaviorKey()},
             {CD::tilingBehaviorGroup(), CD::overflowBehaviorKey()},
             {CD::tilingBehaviorGroup(), CD::focusNewWindowsKey()},
             {CD::tilingBehaviorGroup(), CD::focusFollowsMouseKey()},
             {CD::tilingBehaviorGroup(), CD::restoreFloatedOnLoginKey()},
             {CD::tilingGapsGroup(), CD::smartGapsKey()},
         }},
        {QStringLiteral("tiling-algorithm"),
         {
             {CD::tilingAlgorithmGroup(), CD::defaultKey()},
             {CD::tilingAlgorithmGroup(), CD::splitRatioKey()},
             {CD::tilingAlgorithmGroup(), CD::splitRatioStepKey()},
             {CD::tilingAlgorithmGroup(), CD::masterCountKey()},
             {CD::tilingAlgorithmGroup(), CD::maxWindowsKey()},
             {CD::tilingAlgorithmGroup(), CD::perAlgorithmSettingsKey()},
         }},
        // Only the GLOBAL Windows.* / Gaps.* keys are listed. Per-monitor gap
        // overrides live in the per-screen autotile store (AutotileScreen:*), not
        // in flat config keys, so — like the Tiling Algorithm page's per-monitor
        // split/master/max overrides — they are NOT part of this page's per-page
        // dirty/Reset/Discard. Per-monitor gaps are reset through the Gaps card's
        // scope chip (its override dot + clearPerScreenGapOverride), matching the
        // established per-monitor-override UX; the global footer Save/Discard
        // handles them via the per-screen save path.
        {QStringLiteral("window-appearance"),
         {
             {CD::windowsAppearanceGroup(), CD::showBorderKey()},
             {CD::windowsAppearanceGroup(), CD::borderScopeKey()},
             {CD::windowsAppearanceGroup(), CD::widthKey()},
             {CD::windowsAppearanceGroup(), CD::radiusKey()},
             {CD::windowsAppearanceGroup(), CD::borderColorActiveKey()},
             {CD::windowsAppearanceGroup(), CD::borderColorInactiveKey()},
             {CD::windowsAppearanceGroup(), CD::hideTitleBarsKey()},
             {CD::windowsAppearanceGroup(), CD::titleBarScopeKey()},
             {CD::windowsAppearanceGroup(), CD::focusFadeDurationKey()},
             {CD::windowsAppearanceGroup(), CD::showOpacityTintKey()},
             {CD::windowsAppearanceGroup(), CD::opacityTintScopeKey()},
             {CD::windowsAppearanceGroup(), CD::opacityKey()},
             {CD::windowsAppearanceGroup(), CD::tintStrengthKey()},
             {CD::windowsAppearanceGroup(), CD::tintColorKey()},
             // Window filtering — the Decorations → General page (this
             // window-appearance page) hosts the WindowFilterCard bound to the
             // Decorations.WindowFiltering group.
             {CD::decorationsWindowFilteringGroup(), CD::transientWindowsKey()},
             {CD::decorationsWindowFilteringGroup(), CD::minimumWindowWidthKey()},
             {CD::decorationsWindowFilteringGroup(), CD::minimumWindowHeightKey()},
             {CD::gapsGroup(), CD::innerGapKey()},
             {CD::gapsGroup(), CD::outerGapKey()},
             {CD::gapsGroup(), CD::usePerSideOuterGapKey()},
             {CD::gapsGroup(), CD::outerGapTopKey()},
             {CD::gapsGroup(), CD::outerGapBottomKey()},
             {CD::gapsGroup(), CD::outerGapLeftKey()},
             {CD::gapsGroup(), CD::outerGapRightKey()},
         }},
        {QStringLiteral("editor"),
         {
             {CD::editorShortcutsGroup(), CD::duplicateKey()},
             {CD::editorShortcutsGroup(), CD::splitHorizontalKey()},
             {CD::editorShortcutsGroup(), CD::splitVerticalKey()},
             {CD::editorShortcutsGroup(), CD::fillKey()},
             {CD::editorSnappingGroup(), CD::gridEnabledKey()},
             {CD::editorSnappingGroup(), CD::edgeEnabledKey()},
             {CD::editorSnappingGroup(), CD::intervalXKey()},
             {CD::editorSnappingGroup(), CD::intervalYKey()},
             {CD::editorSnappingGroup(), CD::overrideModifierKey()},
             {CD::editorFillOnDropGroup(), CD::enabledKey()},
             {CD::editorFillOnDropGroup(), CD::modifierKey()},
         }},
    };
    return manifest;
}

const QSet<QString>& SettingsController::validPageNames()
{
    // Keep in sync with the `regPage` / `regVirtual` registrations in
    // `buildApplicationController` in the sibling _pageregistration.cpp — every
    // entry here must resolve
    // to a registered page, otherwise external --page invocations and
    // sidebar navigation will silently fall through to the default page.
    // (The legacy `_pageComponents` Main.qml map this comment used to
    // name was retired when Main.qml moved to PhosphorUi.SettingsAppWindow's
    // framework-driven PageHost Loader, keyed off the registry.)
    static const QSet<QString> pages{
        QStringLiteral("overview"),
        QStringLiteral("layouts"),
        QStringLiteral("snapping-overlay-behavior"),
        QStringLiteral("snapping-overlay-appearance"),
        QStringLiteral("snapping-zoneselector"),
        QStringLiteral("snapping-window-behavior"),
        QStringLiteral("snapping-shaders"),
        QStringLiteral("snapping-shortcuts"),
        QStringLiteral("tiling-behavior"),
        QStringLiteral("tiling-algorithm"),
        QStringLiteral("tiling-shortcuts"),
        QStringLiteral("snapping-ordering"),
        QStringLiteral("tiling-ordering"),
        QStringLiteral("window-appearance"),
        QStringLiteral("decorations-windows"),
        QStringLiteral("decorations-osds"),
        QStringLiteral("decorations-popups"),
        QStringLiteral("decorations-sets"),
        QStringLiteral("decorations-shaders"),
        QStringLiteral("rules"),
        QStringLiteral("editor"),
        QStringLiteral("general"),
        QStringLiteral("about"),
        QStringLiteral("virtualscreens"),
        QStringLiteral("animations-general"),
        QStringLiteral("animations-windows"),
        QStringLiteral("animations-osds"),
        QStringLiteral("animations-overlays"),
        QStringLiteral("animations-desktops"),
        QStringLiteral("animations-editor"),
        QStringLiteral("animations-window-motion"),
        QStringLiteral("animations-side-panels"),
        QStringLiteral("animations-widgets"),
        QStringLiteral("animations-presets"),
        QStringLiteral("animations-motionsets"),
        QStringLiteral("animations-shaders"),
    };
    return pages;
}

} // namespace PlasmaZones

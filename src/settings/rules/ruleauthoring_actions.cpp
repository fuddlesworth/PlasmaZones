// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The action-TYPE half of the action-authoring surface: the picker category
// dispatch, the per-type display labels, and the assembled type list the
// editor's action picker renders. The per-PARAMETER half (labels, hints,
// schema, default payloads) lives in ruleauthoring_actionparams.cpp and the
// per-type hover help in ruleauthoring_actiondescriptions.cpp, so no one of
// the three ladders pushes its file past the size ceiling.

#include "ruleauthoring.h"
#include "ruleauthoring_p.h"

#include "rulemodel.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/RuleAction.h>

#include <QLatin1StringView>
#include <QList>

#include <algorithm>

namespace PlasmaZones::RuleAuthoring {

namespace {

namespace ActionType = PhosphorRules::ActionType;
using PhosphorRules::RuleAction;

// Picker order for every top-level bucket and every Window submenu.
//
// The context-domain buckets come first (0-5), then the ONE window-domain
// bucket. CategoryMenuButton orders a bucket by the smallest order any of its
// items carries, so kWindowPlacement (the smallest of the window numbers) is
// what puts Window last; the rest of the window numbers order the submenus
// *inside* it. Ties fall back to alphabetical, which is what holds Tiling's
// Algorithm/Behavior pair and Scrolling's two indicator submenus in the order
// they have always had.
//
// Keep these in lockstep with each category's action domains in
// RuleAction.cpp: an order that puts a Context action in the window band (or
// the reverse) lands it on the wrong side of the picker's divider.
constexpr int kOrderGaps = 0;
constexpr int kOrderEngine = 1;
constexpr int kOrderSnapping = 2;
constexpr int kOrderTiling = 3;
constexpr int kOrderScrolling = 4;
constexpr int kOrderOverlay = 5;
constexpr int kOrderWindowPlacement = 6;
constexpr int kOrderWindowScrolling = 7;
constexpr int kOrderWindowAppearance = 8;
constexpr int kOrderWindowAnimation = 9;
constexpr int kOrderWindowBehavior = 10;
constexpr int kOrderWindowTabIndicator = 11;
constexpr int kOrderWindowDropIndicator = 12;
constexpr int kOrderOther = 99;

/// Build a `Window/<sub>` category path. EVERY window-domain action goes
/// through here: the Window bucket holds submenus only, never a flat list with
/// a few submenus hanging off the end, so the picker's second level means the
/// same thing everywhere in it.
///
/// The path joins TRANSLATED text with a literal `/`, and CategoryMenuButton
/// splits on the first one to build its tree. A translation containing a slash
/// would therefore restructure the picker rather than just rename a bucket, so
/// `/` is effectively reserved in every category string this file produces. No
/// in-tree locale uses one today.
QString windowSubcategory(const QString& sub)
{
    return PhosphorI18n::tr("Window") + QStringLiteral("/") + sub;
}

/// Group an action type into a picker category.
///
/// The context side derives mostly from the descriptor's `category` field, so
/// a new gap/overlay action needs no change here. `layoutEngine` is the one
/// context category with hand-written per-type dispatch: it splits into
/// Engine, Snapping, Tiling (with Algorithm and Behavior submenus via a `/` in
/// the label) and Scrolling, and a new layoutEngine action falls through to
/// Engine unless it is added to a list below.
///
/// The window side is a single `Window` bucket subdivided by what the action
/// drives — Placement, Scrolling, Appearance, Animation, Behavior, and the two
/// indicator colour families. Three descriptor categories straddle the two
/// sides and carry per-type dispatch for it: `layoutEngine` (the per-app Open*
/// actions are window-domain), `tabIndicator` and `dropIndicator` (their
/// per-window colours are window-domain while the rest of each family is
/// context-domain). A bucket takes its divider side from a single item, so a
/// bucket may never mix domains.
///
/// Takes the descriptor's @p cat rather than re-fetching it: `ActionRegistry::
/// descriptor` returns by value and an ActionDescriptor carries three
/// std::functions, a QStringList and a QList, so the copy allocates. The sole
/// caller has already fetched the descriptor to test `userAuthorable`.
PickerCategory actionCategory(const QString& type, const QString& cat)
{
    if (cat == QLatin1String("gap")) {
        return {PhosphorI18n::tr("Gaps"), kOrderGaps};
    }
    if (cat == QLatin1String("layoutEngine")) {
        // The old flat "Layout & engine" list is split by what each action
        // drives. The engine controls stand alone; the two arrangement engines
        // (snapping and tiling) each get their own category, and tiling is
        // further split into Algorithm and Behavior submenus (a `/` in the
        // label, which CategoryMenuButton renders as a nested submenu).
        if (type == ActionType::SetSnappingLayout) {
            return {PhosphorI18n::tr("Snapping"), kOrderSnapping};
        }
        if (type == ActionType::SetTilingAlgorithm || type == ActionType::SetAlgorithmParam) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Algorithm"), kOrderTiling};
        }
        if (type == ActionType::SetMaxWindows || type == ActionType::SetSplitRatio || type == ActionType::SetMasterCount
            || type == ActionType::SetInsertPosition || type == ActionType::SetOverflowBehavior
            || type == ActionType::SetDragBehavior) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Behavior"), kOrderTiling};
        }
        if (type == ActionType::SetScrollDefaultColumnWidth || type == ActionType::SetCenterFocusedColumn
            || type == ActionType::SetScrollDefaultColumnDisplay || type == ActionType::SetScrollInsertPosition
            || type == ActionType::SetScrollDefaultWindowHeight || type == ActionType::SetScrollingTemplate
            || type == ActionType::SetScrollAlwaysCenterSingleColumn || type == ActionType::SetScrollRespectMinimumSize
            || type == ActionType::SetScrollCropStraddlers || type == ActionType::SetScrollFocusNewWindows
            || type == ActionType::SetScrollSmartGaps || type == ActionType::SetScrollFocusFollowsMouse
            || type == ActionType::SetScrollFocusFollowsMouseMaxScroll
            || type == ActionType::SetScrollStickyWindowHandling || type == ActionType::SetScrollStripAxis) {
            return {PhosphorI18n::tr("Scrolling", "tiling mode name"), kOrderScrolling};
        }
        // The per-app open actions are WINDOW-domain, so they belong to the
        // Window bucket rather than the context-domain Scrolling one. They
        // keep the Scrolling name as a Window submenu: they are the scrolling
        // engine's per-window arm, and the name is what makes them findable.
        if (type == ActionType::OpenColumnWidth || type == ActionType::OpenTabbed
            || type == ActionType::OpenColumnPlacement || type == ActionType::OpenWindowHeight
            || type == ActionType::OpenMaximized || type == ActionType::OpenFocused
            || type == ActionType::OpenFullscreen) {
            return {windowSubcategory(PhosphorI18n::tr("Scrolling", "tiling mode name")), kOrderWindowScrolling};
        }
        // Cross-cutting engine controls: SetEngineMode / DisableEngine /
        // LockContext / DefaultLayoutAssignment. The last two are
        // mode-agnostic — they act on whichever engine owns the context — so
        // they belong here rather than in one engine's bucket.
        return {PhosphorI18n::tr("Engine"), kOrderEngine};
    }
    if (cat == QLatin1String("tabIndicator")) {
        // The three per-window tab colours go to the WINDOW bucket, not the
        // context-domain Scrolling one: CategoryMenuButton takes a top-level
        // bucket's context/window group from the first item to create it in
        // sorted order, and draws the divider from that single group. A
        // mixed-domain bucket therefore renders some of its actions on the
        // wrong side of the divider, and because the sort is over TRANSLATED
        // labels, which side breaks is locale-dependent.
        //
        // Co-locating the whole tab-indicator family under Scrolling reads
        // better as one idea, but it is not worth silently corrupting the
        // divider for the context scroll knobs that share the bucket. The
        // family is still one hop from its twin: Scrolling/Tab indicator holds
        // the context properties, Window/Tab indicator the per-window colours.
        if (type == ActionType::TabColorActive || type == ActionType::TabColorInactive
            || type == ActionType::TabColorUrgent) {
            return {windowSubcategory(PhosphorI18n::tr("Tab indicator")), kOrderWindowTabIndicator};
        }
        // Nested under Scrolling, sharing its order so the whole Scrolling
        // bucket stays put (CategoryMenuButton buckets by the top-level
        // segment and orders buckets by the smallest order in the bucket).
        return {PhosphorI18n::tr("Scrolling", "tiling mode name") + QStringLiteral("/")
                    + PhosphorI18n::tr("Tab indicator"),
                kOrderScrolling};
    }
    if (cat == QLatin1String("dropIndicator")) {
        // Same domain split as tabIndicator directly above, and for the same
        // divider reason: the two per-window colours go to the Window bucket,
        // the context properties nest under Scrolling.
        if (type == ActionType::DropIndicatorColor || type == ActionType::DropIndicatorBorderColor) {
            return {windowSubcategory(PhosphorI18n::tr("Drop indicator")), kOrderWindowDropIndicator};
        }
        return {PhosphorI18n::tr("Scrolling", "tiling mode name") + QStringLiteral("/")
                    + PhosphorI18n::tr("Drop indicator"),
                kOrderScrolling};
    }
    if (cat == QLatin1String("overlay")) {
        return {PhosphorI18n::tr("Overlay"), kOrderOverlay};
    }
    if (cat == QLatin1String("animation")) {
        return {windowSubcategory(PhosphorI18n::tr("Animation")), kOrderWindowAnimation};
    }
    // The border/title-bar family joins the opacity and tint knobs: both are
    // per-window looks, and splitting them left the user guessing which of two
    // adjacent top-level buckets held a given colour.
    if (cat == QLatin1String("appearance") || cat == QLatin1String("borderAppearance")) {
        return {windowSubcategory(PhosphorI18n::tr("Appearance")), kOrderWindowAppearance};
    }
    if (cat == QLatin1String("windowManagement")) {
        // Two knobs that override how a window BEHAVES rather than where it
        // goes or how it looks: its stacking layer, and the pointer
        // scroll-speed multiplier applied while it is hovered. Neither reads
        // as placement, and the scroll multiplier has nothing to do with the
        // scrolling engine, so it must not land in Window/Scrolling.
        if (type == ActionType::SetWindowLayer || type == ActionType::ScrollFactor) {
            return {windowSubcategory(PhosphorI18n::tr("Behavior")), kOrderWindowBehavior};
        }
        // Everything else in the category decides where a window opens, stays,
        // or returns to: snap/float, the screen, desktop and workspace
        // routing, the four
        // restore and unfloat policies, and the two exclusions that opt a
        // window out of placement entirely.
        return {windowSubcategory(PhosphorI18n::tr("Placement")), kOrderWindowPlacement};
    }
    // Reachable only for a descriptor category that does not exist yet — every
    // one the registry currently ships has a branch above. Kept as the
    // extension point so a new category surfaces somewhere rather than nowhere.
    return {PhosphorI18n::tr("Other"), kOrderOther};
}

QString actionTypeLabelImpl(const QString& type)
{
    if (type == ActionType::SetEngineMode) {
        return PhosphorI18n::tr("Set engine mode");
    }
    if (type == ActionType::SetSnappingLayout) {
        return PhosphorI18n::tr("Set snapping layout");
    }
    if (type == ActionType::SetTilingAlgorithm) {
        return PhosphorI18n::tr("Set tiling algorithm");
    }
    if (type == ActionType::SetScrollingTemplate) {
        return PhosphorI18n::tr("Set scrolling template");
    }
    if (type == ActionType::SetMaxWindows) {
        return PhosphorI18n::tr("Set maximum tiled windows");
    }
    if (type == ActionType::SetSplitRatio) {
        return PhosphorI18n::tr("Set split ratio");
    }
    if (type == ActionType::SetMasterCount) {
        return PhosphorI18n::tr("Set master count");
    }
    if (type == ActionType::SetInsertPosition) {
        return PhosphorI18n::tr("Set insert position");
    }
    if (type == ActionType::SetOverflowBehavior) {
        return PhosphorI18n::tr("Set overflow behavior");
    }
    if (type == ActionType::SetDragBehavior) {
        return PhosphorI18n::tr("Set drag behavior");
    }
    if (type == ActionType::SetAlgorithmParam) {
        return PhosphorI18n::tr("Set algorithm parameter");
    }
    if (type == ActionType::SetScrollDefaultColumnWidth) {
        return PhosphorI18n::tr("Set default column width");
    }
    if (type == ActionType::SetCenterFocusedColumn) {
        return PhosphorI18n::tr("Set focused column centering");
    }
    if (type == ActionType::SetScrollDefaultColumnDisplay) {
        return PhosphorI18n::tr("Set default column display");
    }
    if (type == ActionType::SetScrollInsertPosition) {
        return PhosphorI18n::tr("Set new column position");
    }
    if (type == ActionType::SetScrollDefaultWindowHeight) {
        return PhosphorI18n::tr("Set default window height");
    }
    if (type == ActionType::OpenColumnWidth) {
        return PhosphorI18n::tr("Open at column width");
    }
    if (type == ActionType::OpenWindowHeight) {
        return PhosphorI18n::tr("Open at window height");
    }
    if (type == ActionType::OpenTabbed) {
        return PhosphorI18n::tr("Open in a tabbed column");
    }
    if (type == ActionType::OpenColumnPlacement) {
        return PhosphorI18n::tr("Open into column");
    }
    if (type == ActionType::SetScrollAlwaysCenterSingleColumn) {
        return PhosphorI18n::tr("Center a lone column");
    }
    if (type == ActionType::SetScrollRespectMinimumSize) {
        return PhosphorI18n::tr("Respect minimum window sizes");
    }
    if (type == ActionType::SetScrollCropStraddlers) {
        return PhosphorI18n::tr("Crop columns at the screen edge");
    }
    if (type == ActionType::SetScrollFocusNewWindows) {
        return PhosphorI18n::tr("Focus new windows");
    }
    if (type == ActionType::SetScrollSmartGaps) {
        return PhosphorI18n::tr("Drop the outer gaps for a lone column");
    }
    if (type == ActionType::SetScrollFocusFollowsMouse) {
        return PhosphorI18n::tr("Focus follows the mouse");
    }
    if (type == ActionType::SetScrollFocusFollowsMouseMaxScroll) {
        return PhosphorI18n::tr("Limit how far the strip scrolls");
    }
    if (type == ActionType::SetScrollStickyWindowHandling) {
        return PhosphorI18n::tr("Set sticky window handling");
    }
    if (type == ActionType::SetScrollStripAxis) {
        return PhosphorI18n::tr("Set strip direction");
    }
    if (type == ActionType::OpenMaximized) {
        return PhosphorI18n::tr("Open maximized");
    }
    if (type == ActionType::OpenFocused) {
        return PhosphorI18n::tr("Focus when opened");
    }
    if (type == ActionType::OpenFullscreen) {
        return PhosphorI18n::tr("Open in fullscreen");
    }
    // Tab indicator. The labels that could be confused with the tabbed-column
    // actions above say "tab indicator" in full; the ones with no ambiguous
    // counterpart (gap between tabs, corner radius, the three tab colours) say
    // just "tab". The tabbed-column actions change how a column BEHAVES rather
    // than how its indicator is drawn.
    if (type == ActionType::SetTabIndicatorEnabled) {
        return PhosphorI18n::tr("Show the tab indicator");
    }
    if (type == ActionType::SetTabIndicatorStyle) {
        return PhosphorI18n::tr("Set tab indicator style");
    }
    if (type == ActionType::SetTabIndicatorPosition) {
        return PhosphorI18n::tr("Set tab indicator position");
    }
    if (type == ActionType::SetTabIndicatorHideWhenSingleTab) {
        return PhosphorI18n::tr("Hide the tab indicator for a single tab");
    }
    if (type == ActionType::SetTabIndicatorPlaceWithinColumn) {
        return PhosphorI18n::tr("Place the tab indicator inside the column");
    }
    if (type == ActionType::SetTabIndicatorGap) {
        return PhosphorI18n::tr("Set the gap around the tab indicator");
    }
    if (type == ActionType::SetTabIndicatorWidth) {
        return PhosphorI18n::tr("Set tab indicator thickness");
    }
    if (type == ActionType::SetTabIndicatorLength) {
        return PhosphorI18n::tr("Set tab indicator length");
    }
    if (type == ActionType::SetTabIndicatorGapsBetweenTabs) {
        return PhosphorI18n::tr("Set the gap between tabs");
    }
    if (type == ActionType::SetTabIndicatorCornerRadius) {
        return PhosphorI18n::tr("Set tab corner radius");
    }
    if (type == ActionType::SetTabIndicatorActiveColor) {
        return PhosphorI18n::tr("Set the active tab color");
    }
    if (type == ActionType::SetTabIndicatorInactiveColor) {
        return PhosphorI18n::tr("Set the inactive tab color");
    }
    if (type == ActionType::SetTabIndicatorUrgentColor) {
        return PhosphorI18n::tr("Set the urgent tab color");
    }
    // Tab label font. "Tab label" rather than "tab indicator" so these read as
    // the TEXT properties they are, next to the geometry and colour actions
    // above that shape the pill itself. There is no size action: the label is
    // auto-fitted to the pill's thickness.
    if (type == ActionType::SetTabIndicatorFontFamily) {
        return PhosphorI18n::tr("Set the tab label font");
    }
    if (type == ActionType::SetTabIndicatorFontWeight) {
        return PhosphorI18n::tr("Set the tab label weight");
    }
    if (type == ActionType::SetTabIndicatorFontItalic) {
        return PhosphorI18n::tr("Make tab labels italic");
    }
    if (type == ActionType::SetTabIndicatorFontUnderline) {
        return PhosphorI18n::tr("Underline tab labels");
    }
    if (type == ActionType::SetTabIndicatorFontStrikeout) {
        return PhosphorI18n::tr("Strike through tab labels");
    }
    if (type == ActionType::TabColorActive) {
        return PhosphorI18n::tr("Set this window's active tab color");
    }
    if (type == ActionType::TabColorInactive) {
        return PhosphorI18n::tr("Set this window's inactive tab color");
    }
    if (type == ActionType::TabColorUrgent) {
        return PhosphorI18n::tr("Set this window's urgent tab color");
    }
    // Drop indicator. "Drop indicator" rather than "drag indicator" so the
    // labels match the settings card the same properties live on.
    if (type == ActionType::SetDropIndicatorEnabled) {
        return PhosphorI18n::tr("Show the drop indicator");
    }
    if (type == ActionType::SetDropIndicatorColor) {
        return PhosphorI18n::tr("Set the drop indicator fill color");
    }
    if (type == ActionType::SetDropIndicatorBorderColor) {
        return PhosphorI18n::tr("Set the drop indicator border color");
    }
    if (type == ActionType::SetDropIndicatorOpacity) {
        return PhosphorI18n::tr("Set the drop indicator fill opacity");
    }
    if (type == ActionType::SetDropIndicatorBorderWidth) {
        return PhosphorI18n::tr("Set the drop indicator border width");
    }
    if (type == ActionType::SetDropIndicatorBorderRadius) {
        return PhosphorI18n::tr("Set the drop indicator corner radius");
    }
    // The two per-window colours say "when dragging this window" rather than
    // "this window's", unlike the tab colours: a tab colour paints ON the
    // matched window, while these paint a slot elsewhere on screen BECAUSE
    // that window is the one being dragged.
    if (type == ActionType::DropIndicatorColor) {
        return PhosphorI18n::tr("Set the drop indicator fill color when dragging this window");
    }
    if (type == ActionType::DropIndicatorBorderColor) {
        return PhosphorI18n::tr("Set the drop indicator border color when dragging this window");
    }
    if (type == ActionType::DisableEngine) {
        return PhosphorI18n::tr("Disable engine");
    }
    if (type == ActionType::LockContext) {
        return PhosphorI18n::tr("Lock layout");
    }
    if (type == ActionType::DefaultLayoutAssignment) {
        return PhosphorI18n::tr("Assign default layout");
    }
    if (type == ActionType::Exclude) {
        // The exclusion family shares one shape ("Exclude from <scope>") built
        // on the app's own umbrella terms: "placement" covers the tiling,
        // snapping, and scrolling engines (the excludeApp template description
        // spells that out), "decorations" covers borders and decoration packs.
        // The blanket form names both scopes so it reads distinctly beside the
        // placement-only sibling in the same picker bucket.
        return PhosphorI18n::tr("Exclude from placement and decorations");
    }
    if (type == ActionType::ExcludePlacement) {
        return PhosphorI18n::tr("Exclude from placement");
    }
    if (type == ActionType::Float) {
        return PhosphorI18n::tr("Float window");
    }
    if (type == ActionType::SnapToZone) {
        return PhosphorI18n::tr("Snap to zones");
    }
    if (type == ActionType::RestorePosition) {
        return PhosphorI18n::tr("Restore previous position");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin) {
        // Named after the setting it overrides ("Restore windows to their
        // previous zone"), not the wire id: the gate fires on every reopen,
        // during the session and after a logout, so "on login" undersold it
        // and hid the rule from anyone searching by the setting's name
        // (discussion #889 asked for exactly this control, not knowing it
        // shipped under the old label).
        return PhosphorI18n::tr("Restore to previous zone");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap) {
        return PhosphorI18n::tr("Restore size on unsnap");
    }
    if (type == ActionType::SetUnfloatFallbackToZone) {
        return PhosphorI18n::tr("Fall back to a zone on unfloat");
    }
    if (type == ActionType::SetWindowLayer) {
        return PhosphorI18n::tr("Set window layer");
    }
    if (type == ActionType::ScrollFactor) {
        return PhosphorI18n::tr("Set scroll speed");
    }
    if (type == ActionType::SetOsdEnabled) {
        return PhosphorI18n::tr("Show on-screen displays");
    }
    if (type == ActionType::SetDragSelectorEnabled) {
        // "Drag selector" deliberately matches NEITHER settings card label
        // ("Zone selector popup" / "Strip selector popup"): one action
        // governs both variants, so unlike the drop-indicator family (whose
        // label matches its single settings card, see the note there) there
        // is no one card to echo, and the umbrella term is the honest name.
        return PhosphorI18n::tr("Show the drag selector");
    }
    if (type == ActionType::OverrideAnimationShader) {
        return PhosphorI18n::tr("Override animation shader");
    }
    if (type == ActionType::OverrideDecorationChain) {
        return PhosphorI18n::tr("Override decoration packs");
    }
    if (type == ActionType::OverrideAnimationTiming) {
        return PhosphorI18n::tr("Override animation duration");
    }
    if (type == ActionType::OverrideAnimationCurve) {
        return PhosphorI18n::tr("Override animation curve");
    }
    if (type == ActionType::SetOpacity) {
        return PhosphorI18n::tr("Set opacity");
    }
    if (type == ActionType::OverrideOverlayShader) {
        return PhosphorI18n::tr("Override overlay shader");
    }
    if (type == ActionType::OverrideOverlayStyle) {
        return PhosphorI18n::tr("Override overlay style");
    }
    if (type == ActionType::SetOverlayHighlightColor) {
        return PhosphorI18n::tr("Set overlay highlight color");
    }
    if (type == ActionType::SetOverlayInactiveColor) {
        return PhosphorI18n::tr("Set overlay inactive color");
    }
    if (type == ActionType::SetOverlayBorderColor) {
        return PhosphorI18n::tr("Set overlay border color");
    }
    if (type == ActionType::SetOverlayActiveOpacity) {
        return PhosphorI18n::tr("Set overlay active opacity");
    }
    if (type == ActionType::SetOverlayInactiveOpacity) {
        return PhosphorI18n::tr("Set overlay inactive opacity");
    }
    if (type == ActionType::SetOverlayBorderWidth) {
        return PhosphorI18n::tr("Set overlay border width");
    }
    if (type == ActionType::SetOverlayBorderRadius) {
        return PhosphorI18n::tr("Set overlay corner radius");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return PhosphorI18n::tr("Show zone numbers");
    }
    if (type == ActionType::ExcludeAnimations) {
        return PhosphorI18n::tr("Exclude from animations");
    }
    if (type == ActionType::ExcludeDecorations) {
        // "Decorations" here means the app's own decorations (borders and
        // decoration packs), matching the blanket Exclude label. It does NOT
        // touch the title bar, which KWin's vocabulary also calls the window
        // decoration; SetHideTitleBar owns that. The action's own hover help
        // in ruleauthoring_actiondescriptions.cpp spells out the real scope.
        return PhosphorI18n::tr("Exclude from decorations");
    }
    if (type == ActionType::SetHideTitleBar) {
        // Affirmative verb phrase like the other boolean action labels (e.g.
        // SetBorderVisible's "Show border"); the on-state is "hide". The
        // parameter toggle's "(off = force visible)" wording carries the
        // tri-state detail (off forces the title bar visible even where the
        // mode would otherwise hide it).
        return PhosphorI18n::tr("Hide title bars");
    }
    if (type == ActionType::SetBorderVisible) {
        return PhosphorI18n::tr("Show border");
    }
    if (type == ActionType::SetBorderWidth) {
        return PhosphorI18n::tr("Set border width");
    }
    if (type == ActionType::SetBorderRadius) {
        return PhosphorI18n::tr("Set corner radius");
    }
    if (type == ActionType::SetBorderColorActive) {
        return PhosphorI18n::tr("Set focused border color");
    }
    if (type == ActionType::SetBorderColorInactive) {
        return PhosphorI18n::tr("Set unfocused border color");
    }
    if (type == ActionType::SetOpacityTintVisible) {
        return PhosphorI18n::tr("Show opacity and tint");
    }
    if (type == ActionType::SetTintStrength) {
        return PhosphorI18n::tr("Set tint strength");
    }
    if (type == ActionType::SetTintColor) {
        return PhosphorI18n::tr("Set tint color");
    }
    if (type == ActionType::SetInnerGap) {
        return PhosphorI18n::tr("Set inner gap");
    }
    if (type == ActionType::SetOuterGap) {
        return PhosphorI18n::tr("Set outer gap");
    }
    if (type == ActionType::SetUsePerSideOuterGap) {
        return PhosphorI18n::tr("Use per-side outer gaps");
    }
    if (type == ActionType::SetOuterGapTop) {
        return PhosphorI18n::tr("Set top gap");
    }
    if (type == ActionType::SetOuterGapBottom) {
        return PhosphorI18n::tr("Set bottom gap");
    }
    if (type == ActionType::SetOuterGapLeft) {
        return PhosphorI18n::tr("Set left gap");
    }
    if (type == ActionType::SetOuterGapRight) {
        return PhosphorI18n::tr("Set right gap");
    }
    if (type == ActionType::RouteToScreen) {
        return PhosphorI18n::tr("Open on monitor");
    }
    if (type == ActionType::RouteToDesktop) {
        return PhosphorI18n::tr("Open on desktop");
    }
    if (type == ActionType::RouteToWorkspace) {
        return PhosphorI18n::tr("Open on workspace");
    }
    return RuleModel::actionTypeFallbackLabel(type);
}

} // namespace

QString boolActionStateLabel(const QString& type, bool on)
{
    if (type == ActionType::RestorePosition) {
        return on ? PhosphorI18n::tr("Restore previous position") : PhosphorI18n::tr("Don't restore previous position");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin) {
        return on ? PhosphorI18n::tr("Restore to previous zone") : PhosphorI18n::tr("Don't restore to previous zone");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap) {
        return on ? PhosphorI18n::tr("Restore size on unsnap") : PhosphorI18n::tr("Keep zone size on unsnap");
    }
    if (type == ActionType::SetUnfloatFallbackToZone) {
        return on ? PhosphorI18n::tr("Fall back to a zone on unfloat")
                  : PhosphorI18n::tr("Stay floating when no zone is remembered");
    }
    if (type == ActionType::SetHideTitleBar) {
        return on ? PhosphorI18n::tr("Hide title bars") : PhosphorI18n::tr("Show title bars");
    }
    if (type == ActionType::LockContext) {
        return on ? PhosphorI18n::tr("Lock layout") : PhosphorI18n::tr("Don't lock layout");
    }
    if (type == ActionType::DefaultLayoutAssignment) {
        return on ? PhosphorI18n::tr("Assign default layout") : PhosphorI18n::tr("Don't assign default layout");
    }
    if (type == ActionType::SetBorderVisible) {
        return on ? PhosphorI18n::tr("Show border") : PhosphorI18n::tr("Hide border");
    }
    if (type == ActionType::SetOpacityTintVisible) {
        return on ? PhosphorI18n::tr("Show opacity and tint") : PhosphorI18n::tr("Hide opacity and tint");
    }
    if (type == ActionType::SetUsePerSideOuterGap) {
        return on ? PhosphorI18n::tr("Per-side outer gaps") : PhosphorI18n::tr("Uniform outer gap");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return on ? PhosphorI18n::tr("Show zone numbers") : PhosphorI18n::tr("Hide zone numbers");
    }
    if (type == ActionType::SetOsdEnabled) {
        // On is a force, not an absence: it shows the popups here even when
        // the global toggles are off, so both phrases name outcomes.
        return on ? PhosphorI18n::tr("Show on-screen displays here") : PhosphorI18n::tr("Hide on-screen displays here");
    }
    if (type == ActionType::SetDragSelectorEnabled) {
        // On is a force like its OSD sibling: it offers the popup here even
        // when the global selector toggle is off, so both phrases name
        // outcomes.
        return on ? PhosphorI18n::tr("Show the drag selector here") : PhosphorI18n::tr("Hide the drag selector here");
    }
    if (type == ActionType::OpenTabbed) {
        // Off is not inert: it forces a normal column even where the context
        // default is tabbed, so the off phrase names that outcome.
        return on ? PhosphorI18n::tr("Open in a tabbed column") : PhosphorI18n::tr("Open in a normal column");
    }
    if (type == ActionType::OpenMaximized) {
        // Off is inert today (there is no context default that maximizes),
        // but the phrase still names the outcome for symmetry with the family.
        return on ? PhosphorI18n::tr("Open maximized") : PhosphorI18n::tr("Open at the default width");
    }
    // The scrolling behaviour toggles. Each off phrase names the OUTCOME, the
    // same discipline the tab-indicator family follows: these override a
    // config value, so switching one off is an instruction rather than an
    // absence.
    if (type == ActionType::SetScrollAlwaysCenterSingleColumn) {
        return on ? PhosphorI18n::tr("Center a lone column") : PhosphorI18n::tr("Leave a lone column where it sits");
    }
    if (type == ActionType::SetScrollRespectMinimumSize) {
        return on ? PhosphorI18n::tr("Respect minimum window sizes") : PhosphorI18n::tr("Ignore minimum window sizes");
    }
    if (type == ActionType::SetScrollCropStraddlers) {
        return on ? PhosphorI18n::tr("Crop columns at the screen edge")
                  : PhosphorI18n::tr("Keep columns whole at the screen edge");
    }
    if (type == ActionType::SetScrollFocusNewWindows) {
        return on ? PhosphorI18n::tr("Focus new windows") : PhosphorI18n::tr("Keep focus where it is");
    }
    if (type == ActionType::SetScrollSmartGaps) {
        return on ? PhosphorI18n::tr("Drop the outer gaps for a lone column")
                  : PhosphorI18n::tr("Keep the outer gaps for a lone column");
    }
    if (type == ActionType::SetScrollFocusFollowsMouse) {
        return on ? PhosphorI18n::tr("Focus follows the mouse") : PhosphorI18n::tr("Focus stays until you click");
    }
    if (type == ActionType::OpenFocused) {
        // Both polarities are live: on forces focus past a focus-new-windows
        // OFF global, off withholds it past an ON one.
        return on ? PhosphorI18n::tr("Focus the window when it opens")
                  : PhosphorI18n::tr("Keep focus where it was when it opens");
    }
    if (type == ActionType::OpenFullscreen) {
        // Off is a veto, not an absence: it blocks the app's own fullscreen
        // request at open, so the phrase names that outcome.
        return on ? PhosphorI18n::tr("Open in fullscreen") : PhosphorI18n::tr("Block fullscreen at open");
    }
    // Tab indicator. Each off phrase names an OUTCOME rather than a negation,
    // for the reason OpenTabbed's does: these override a context or config
    // value, so switching one off is an instruction, not an absence.
    if (type == ActionType::SetTabIndicatorEnabled) {
        return on ? PhosphorI18n::tr("Show the tab indicator") : PhosphorI18n::tr("Hide the tab indicator");
    }
    if (type == ActionType::SetTabIndicatorHideWhenSingleTab) {
        return on ? PhosphorI18n::tr("Hide the tab indicator for a single tab")
                  : PhosphorI18n::tr("Show the tab indicator for a single tab");
    }
    if (type == ActionType::SetTabIndicatorPlaceWithinColumn) {
        return on ? PhosphorI18n::tr("Tab indicator inside the column")
                  : PhosphorI18n::tr("Tab indicator beside the column");
    }
    // Tab label style flags. Same outcome-not-negation phrasing, and the off
    // phrases name the plain form rather than saying "not italic", so a rule
    // list reads as an instruction either way.
    if (type == ActionType::SetTabIndicatorFontItalic) {
        return on ? PhosphorI18n::tr("Italic tab labels") : PhosphorI18n::tr("Upright tab labels");
    }
    if (type == ActionType::SetTabIndicatorFontUnderline) {
        return on ? PhosphorI18n::tr("Underlined tab labels") : PhosphorI18n::tr("Tab labels without an underline");
    }
    if (type == ActionType::SetTabIndicatorFontStrikeout) {
        return on ? PhosphorI18n::tr("Struck-through tab labels")
                  : PhosphorI18n::tr("Tab labels without a line through them");
    }
    // Drop indicator, same outcome-not-negation phrasing as the tab family.
    if (type == ActionType::SetDropIndicatorEnabled) {
        return on ? PhosphorI18n::tr("Show the drop indicator") : PhosphorI18n::tr("Hide the drop indicator");
    }
    return QString();
}

QString actionTypeLabel(const QString& typeWire)
{
    return actionTypeLabelImpl(typeWire);
}

QVariantList actionTypes()
{
    const PhosphorRules::ActionRegistry& registry = PhosphorRules::ActionRegistry::instance();

    struct TypeEntry
    {
        QString type;
        QString categoryLabel;
        int categoryOrder;
        QString domain;
    };
    QList<TypeEntry> entries;
    for (const QString& type : registry.registeredTypes()) {
        const auto desc = registry.descriptor(type);
        // `userAuthorable` defaults true and no descriptor in the tree sets it
        // false today; the filter is the declared opt-out, not dead weight.
        if (!desc.has_value() || !desc->userAuthorable) {
            continue;
        }
        // One descriptor fetch per type feeds the category, the label and the
        // domain below. `descriptor()` returns by value and the copy allocates.
        const PickerCategory acat = actionCategory(type, desc->category);
        const QString domainStr =
            desc->domain == PhosphorRules::ActionDomain::Context ? QStringLiteral("context") : QStringLiteral("window");
        entries.append({type, acat.label, acat.order, domainStr});
    }
    // Sorted only to make this list deterministic across registry iteration
    // orders. The sole consumer, CategoryMenuButton, re-sorts every item
    // alphabetically by label and derives its bucket order from the smallest
    // categoryOrder in each bucket, so nothing downstream can observe a
    // within-category order chosen here. ActionDescriptor::displayOrder is
    // deliberately not consulted: honouring it would take a change in the
    // picker, not another comparator leg here.
    std::sort(entries.begin(), entries.end(), [](const TypeEntry& a, const TypeEntry& b) {
        if (a.categoryOrder != b.categoryOrder) {
            return a.categoryOrder < b.categoryOrder;
        }
        return a.type < b.type;
    });

    QVariantList out;
    for (const TypeEntry& e : entries) {
        QVariantMap entry;
        entry[QStringLiteral("value")] = e.type;
        entry[QStringLiteral("label")] = actionTypeLabelImpl(e.type);
        entry[QStringLiteral("description")] = actionDescription(e.type);
        entry[QStringLiteral("params")] = paramsForActionType(e.type);
        entry[QStringLiteral("category")] = e.categoryLabel;
        entry[QStringLiteral("categoryOrder")] = e.categoryOrder;
        entry[QStringLiteral("domain")] = e.domain;
        // The picker draws a divider between top-level categories whose group
        // differs — context-domain categories above, window-domain below.
        entry[QStringLiteral("categoryGroup")] = e.domain;
        out.append(entry);
    }
    return out;
}

} // namespace PlasmaZones::RuleAuthoring

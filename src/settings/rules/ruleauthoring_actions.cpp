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

/// Group an action type into a picker category. Most categories derive
/// straight from the descriptor's `category` field, so a new action in an
/// existing category needs no change here. Three categories are exceptions and
/// carry hand-written per-type dispatch: `tabIndicator` and `dropIndicator`
/// (whose per-window colours must leave the Scrolling bucket so it stays
/// single-domain) and `layoutEngine`,
/// which is split by action type into Engine, Snapping, Tiling (the last
/// with Algorithm and Behavior submenus via a `/` in the label), a
/// top-level Scrolling (the context-domain scroll knobs), and a
/// Window/Scrolling submenu (the per-window open actions, which
/// deliberately cross into the window-domain order band); a new
/// layoutEngine action lands in Engine unless added to the dispatch below.
PickerCategory actionCategory(const QString& type)
{
    const auto desc = PhosphorRules::ActionRegistry::instance().descriptor(type);
    if (!desc.has_value()) {
        return {PhosphorI18n::tr("Other"), 99};
    }
    const QString& cat = desc->category;
    // Two groups: the context-domain categories (resolved per
    // screen/desktop/activity/mode) come first (orders 0-5), then the
    // window-domain categories (orders 6-9). Order within a group is
    // curated, not alphabetical. Keep these orders in lockstep with each
    // category's action domains in RuleAction.cpp.
    if (cat == QLatin1String("gap")) {
        return {PhosphorI18n::tr("Gaps"), 0};
    }
    if (cat == QLatin1String("layoutEngine")) {
        // The old flat "Layout & engine" list is split by what each action
        // drives. The engine controls stand alone; the two arrangement engines
        // (snapping and tiling) each get their own category, and tiling is
        // further split into Algorithm and Behavior submenus (a `/` in the
        // label, which CategoryMenuButton renders as a nested submenu).
        if (type == ActionType::SetSnappingLayout) {
            return {PhosphorI18n::tr("Snapping"), 2};
        }
        if (type == ActionType::SetTilingAlgorithm || type == ActionType::SetAlgorithmParam) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Algorithm"), 3};
        }
        if (type == ActionType::SetMaxWindows || type == ActionType::SetSplitRatio || type == ActionType::SetMasterCount
            || type == ActionType::SetInsertPosition || type == ActionType::SetOverflowBehavior
            || type == ActionType::SetDragBehavior) {
            return {PhosphorI18n::tr("Tiling") + QStringLiteral("/") + PhosphorI18n::tr("Behavior"), 3};
        }
        if (type == ActionType::SetScrollDefaultColumnWidth || type == ActionType::SetCenterFocusedColumn
            || type == ActionType::SetScrollDefaultColumnDisplay || type == ActionType::SetScrollInsertPosition
            || type == ActionType::SetScrollDefaultWindowHeight || type == ActionType::SetScrollingTemplate
            || type == ActionType::SetScrollAlwaysCenterSingleColumn || type == ActionType::SetScrollRespectMinimumSize
            || type == ActionType::SetScrollCropStraddlers || type == ActionType::SetScrollFocusNewWindows
            || type == ActionType::SetScrollSmartGaps || type == ActionType::SetScrollFocusFollowsMouse
            || type == ActionType::SetScrollStickyWindowHandling || type == ActionType::SetScrollStripAxis) {
            return {PhosphorI18n::tr("Scrolling", "tiling mode name"), 4};
        }
        // The per-app open actions are WINDOW-domain: they must sit in the
        // window block so the picker's context/window divider stays honest
        // (a mixed top-level category takes its group from the first item
        // and would render window actions above the divider). Nested under
        // Window as a Scrolling submenu for discoverability.
        if (type == ActionType::OpenColumnWidth || type == ActionType::OpenTabbed
            || type == ActionType::OpenColumnPlacement || type == ActionType::OpenWindowHeight
            || type == ActionType::OpenMaximized || type == ActionType::OpenFocused
            || type == ActionType::OpenFullscreen) {
            // Order 8, the same as plain Window: CategoryMenuButton buckets by
            // the TOP-LEVEL segment of the category path and orders the buckets
            // by the smallest order any item in the bucket carries, so
            // "Window/Scrolling" lands in the Window bucket either way and a
            // different number here could only ever pull the whole Window
            // bucket earlier. Orders are per top-level category, not per path:
            // the Tiling submenus already share 3 with plain Tiling.
            return {PhosphorI18n::tr("Window") + QStringLiteral("/")
                        + PhosphorI18n::tr("Scrolling", "tiling mode name"),
                    8};
        }
        // Cross-cutting engine controls: SetEngineMode / DisableEngine /
        // LockContext / DefaultLayoutAssignment. The last two are
        // mode-agnostic — they act on whichever engine owns the context — so
        // they belong here rather than in one engine's bucket.
        return {PhosphorI18n::tr("Engine"), 1};
    }
    if (cat == QLatin1String("tabIndicator")) {
        // The three per-window tab colours go to the WINDOW bucket, not the
        // Scrolling one, for the same reason the open-action branch above
        // exists: CategoryMenuButton takes a top-level bucket's context/window
        // group from the first item to create it in sorted order, and draws
        // the divider from that single group. A mixed-domain bucket therefore
        // renders some of its actions on the wrong side of the divider — and
        // because the sort is over TRANSLATED labels, which side breaks is
        // locale-dependent.
        //
        // Co-locating the whole tab-indicator family under Scrolling reads
        // better as one idea, but it is not worth silently corrupting the
        // divider for the context scroll knobs that share the bucket.
        if (type == ActionType::TabColorActive || type == ActionType::TabColorInactive
            || type == ActionType::TabColorUrgent) {
            return {PhosphorI18n::tr("Window") + QStringLiteral("/") + PhosphorI18n::tr("Tab indicator"), 8};
        }
        // Nested under Scrolling, sharing its order 4 so the whole Scrolling
        // bucket stays put (CategoryMenuButton buckets by the top-level
        // segment and orders buckets by the smallest order in the bucket).
        return {PhosphorI18n::tr("Scrolling", "tiling mode name") + QStringLiteral("/")
                    + PhosphorI18n::tr("Tab indicator"),
                4};
    }
    if (cat == QLatin1String("dropIndicator")) {
        // Same domain split as tabIndicator directly above, and for the same
        // divider reason: the two per-window colours go to the Window bucket,
        // the context properties nest under Scrolling.
        if (type == ActionType::DropIndicatorColor || type == ActionType::DropIndicatorBorderColor) {
            return {PhosphorI18n::tr("Window") + QStringLiteral("/") + PhosphorI18n::tr("Drop indicator"), 8};
        }
        return {PhosphorI18n::tr("Scrolling", "tiling mode name") + QStringLiteral("/")
                    + PhosphorI18n::tr("Drop indicator"),
                4};
    }
    if (cat == QLatin1String("overlay")) {
        return {PhosphorI18n::tr("Overlay"), 5};
    }
    if (cat == QLatin1String("animation")) {
        return {PhosphorI18n::tr("Animation"), 6};
    }
    if (cat == QLatin1String("appearance") || cat == QLatin1String("borderAppearance")) {
        return {PhosphorI18n::tr("Appearance"), 7};
    }
    if (cat == QLatin1String("windowManagement")) {
        return {PhosphorI18n::tr("Window"), 8};
    }
    return {PhosphorI18n::tr("Other"), 99};
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
        return PhosphorI18n::tr("Set max tiled windows");
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
        return PhosphorI18n::tr("Smart gaps");
    }
    if (type == ActionType::SetScrollFocusFollowsMouse) {
        return PhosphorI18n::tr("Focus follows the mouse");
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
    // Tab indicator. The labels say "tab indicator" rather than just "tabs" so
    // they cannot be mistaken for the tabbed-column actions above, which
    // change how a column BEHAVES rather than how its indicator is drawn.
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
        return PhosphorI18n::tr("Default layout assignment");
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
        return PhosphorI18n::tr("Snap to zone(s)");
    }
    if (type == ActionType::RestorePosition) {
        return PhosphorI18n::tr("Restore position on login");
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
        return PhosphorI18n::tr("Set overlay shader");
    }
    if (type == ActionType::OverrideOverlayStyle) {
        return PhosphorI18n::tr("Set overlay style");
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
        // decoration; SetHideTitleBar owns that, and the undecorateApp
        // template description spells out the real scope.
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
    return RuleModel::actionTypeFallbackLabel(type);
}

} // namespace

QString boolActionStateLabel(const QString& type, bool on)
{
    namespace ActionType = PhosphorRules::ActionType;
    if (type == ActionType::RestorePosition) {
        return on ? PhosphorI18n::tr("Restore position on login") : PhosphorI18n::tr("Don't restore position on login");
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
    };
    QList<TypeEntry> entries;
    for (const QString& type : registry.registeredTypes()) {
        const auto desc = registry.descriptor(type);
        if (!desc.has_value() || !desc->userAuthorable) {
            continue;
        }
        const PickerCategory acat = actionCategory(type);
        entries.append({type, acat.label, acat.order});
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
        RuleAction probe;
        probe.type = e.type;
        const auto domain = registry.domainFor(probe);
        const QString domainStr =
            domain == PhosphorRules::ActionDomain::Context ? QStringLiteral("context") : QStringLiteral("window");
        entry[QStringLiteral("domain")] = domainStr;
        // The picker draws a divider between top-level categories whose group
        // differs — context-domain categories above, window-domain below.
        entry[QStringLiteral("categoryGroup")] = domainStr;
        out.append(entry);
    }
    return out;
}

} // namespace PlasmaZones::RuleAuthoring

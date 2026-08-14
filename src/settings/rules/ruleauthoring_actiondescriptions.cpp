// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-ACTION descriptions for the rule editor — the action-side mirror of
// fieldDescription() (ruleauthoring.cpp), surfaced as the hover tooltip on the
// action row's info icon. One concise plain-prose line per registered action
// type; the test canary (test_rule_controller) sweeps
// ActionRegistry::registeredTypes() and fails on any type this ladder misses,
// so adding an action without a description is a red test, not a silent blank.
//
// Own translation unit rather than ruleauthoring_actions.cpp: the ladder
// carries one entry per registered action type, and that file already sits at
// the size guideline.

#include "ruleauthoring.h"

#include "phosphor_i18n.h"

#include <PhosphorRules/RuleAction.h>

namespace PlasmaZones::RuleAuthoring {

QString actionDescription(const QString& type)
{
    namespace ActionType = PhosphorRules::ActionType;
    // ── Engine / assignment (context) ──
    if (type == ActionType::SetEngineMode) {
        return PhosphorI18n::tr(
            "Sets which placement engine (snapping, tiling or scrolling) runs on the matched "
            "screen, desktop or activity.");
    }
    if (type == ActionType::SetSnappingLayout) {
        return PhosphorI18n::tr("Assigns a snapping layout to the matched screen, desktop or activity.");
    }
    if (type == ActionType::SetTilingAlgorithm) {
        return PhosphorI18n::tr("Assigns a tiling algorithm to the matched screen, desktop or activity.");
    }
    if (type == ActionType::SetScrollingTemplate) {
        return PhosphorI18n::tr("Assigns a scrolling column template to the matched screen, desktop or activity.");
    }
    if (type == ActionType::DisableEngine) {
        return PhosphorI18n::tr("Turns the named engine off for the matched screen, desktop or activity.");
    }
    if (type == ActionType::LockContext) {
        return PhosphorI18n::tr("Locks the active layout for the matched context so it cannot be switched.");
    }
    if (type == ActionType::DefaultLayoutAssignment) {
        return PhosphorI18n::tr(
            "Allows or suppresses the automatic default layout for the matched context. Off means "
            "no engine activates there until you assign one yourself.");
    }
    if (type == ActionType::SetOsdEnabled) {
        return PhosphorI18n::tr(
            "Shows or hides on-screen displays for the matched screen, desktop or activity. Off "
            "silences the layout and navigation popups there. On shows them even when the global "
            "toggles are off, though the None display style still hides everything.");
    }
    if (type == ActionType::SetDragSelectorEnabled) {
        return PhosphorI18n::tr(
            "Shows or hides the drag selector popup for the matched screen, desktop or activity. "
            "Off means dragging a window near the trigger edge offers no picker there. On offers "
            "it even when the global selector toggle for that screen is off.");
    }
    // ── Placement (window) ──
    if (type == ActionType::Exclude) {
        return PhosphorI18n::tr(
            "Keeps matching windows out of PlasmaZones entirely, so they get no placement and no decorations.");
    }
    if (type == ActionType::ExcludePlacement) {
        return PhosphorI18n::tr(
            "Keeps matching windows out of snapping, tiling and scrolling while their decorations "
            "and animations still apply.");
    }
    if (type == ActionType::ExcludeAnimations) {
        return PhosphorI18n::tr("Turns off every PlasmaZones animation for matching windows.");
    }
    if (type == ActionType::ExcludeDecorations) {
        return PhosphorI18n::tr(
            "Turns off PlasmaZones borders and decoration packs for matching windows. Placement "
            "and animations are untouched.");
    }
    if (type == ActionType::Float) {
        return PhosphorI18n::tr(
            "Opens matching windows floating instead of placing them. A floated window can still "
            "be snapped or tiled by hand later.");
    }
    if (type == ActionType::SnapToZone) {
        return PhosphorI18n::tr(
            "Snaps matching windows into the given zone numbers when they open. Several numbers "
            "span their combined area.");
    }
    if (type == ActionType::RouteToScreen) {
        return PhosphorI18n::tr("Moves matching windows to a chosen monitor when they open.");
    }
    if (type == ActionType::RouteToDesktop) {
        return PhosphorI18n::tr("Moves matching windows to a chosen virtual desktop when they open.");
    }
    if (type == ActionType::RestorePosition) {
        return PhosphorI18n::tr(
            "Controls whether a matching floated window returns to its remembered position and "
            "monitor on login, overriding the global setting.");
    }
    if (type == ActionType::SetRestoreToZoneOnLogin) {
        return PhosphorI18n::tr(
            "Controls whether a matching window returns to its previous zone when it reopens, during the session or "
            "after a logout. Overrides the Restore windows to their previous zone setting, so an app like a browser "
            "can be left out while every other window still restores.");
    }
    if (type == ActionType::SetRestoreSizeOnUnsnap) {
        return PhosphorI18n::tr(
            "Controls whether a matching window gets its original size back when unsnapped, "
            "overriding the global setting.");
    }
    if (type == ActionType::SetUnfloatFallbackToZone) {
        return PhosphorI18n::tr(
            "When a matching floating window is unfloated without a remembered zone, places it "
            "into a zone anyway. Overrides the global fallback setting for this window.");
    }
    if (type == ActionType::SetWindowLayer) {
        return PhosphorI18n::tr(
            "Keeps matching windows above or below other windows, for example floating windows "
            "above tiled ones when paired with the Floating condition.");
    }
    // ── niri open rules (window, scrolling) ──
    if (type == ActionType::OpenColumnWidth) {
        return PhosphorI18n::tr("Sets the width a matching window's column opens at on a scrolling screen.");
    }
    if (type == ActionType::OpenWindowHeight) {
        return PhosphorI18n::tr("Sets the height a matching window takes inside its column when it opens.");
    }
    if (type == ActionType::OpenTabbed) {
        return PhosphorI18n::tr(
            "Opens a matching window's column tabbed, or forces it normal where the default is "
            "tabbed.");
    }
    if (type == ActionType::OpenColumnPlacement) {
        return PhosphorI18n::tr("Chooses whether a matching window opens its own column or joins the focused one.");
    }
    if (type == ActionType::OpenMaximized) {
        return PhosphorI18n::tr(
            "Opens matching windows maximized, so their column fills the full width of the work "
            "area when they first appear.");
    }
    if (type == ActionType::OpenFocused) {
        return PhosphorI18n::tr(
            "Gives matching windows keyboard focus as soon as they open, or keeps focus where it "
            "was when set to off.");
    }
    if (type == ActionType::OpenFullscreen) {
        return PhosphorI18n::tr(
            "Opens matching windows in fullscreen mode. Off stops apps from starting in "
            "fullscreen on their own.");
    }
    if (type == ActionType::ScrollFactor) {
        return PhosphorI18n::tr(
            "Scales mouse wheel and touchpad scrolling speed inside matching windows. Below 100% "
            "slows it down and above 100% speeds it up.");
    }
    // ── Animation and decoration overrides (window) ──
    if (type == ActionType::OverrideAnimationShader) {
        return PhosphorI18n::tr(
            "Uses a different animation shader for one event on matching windows. An empty choice "
            "turns that event's shader off for them.");
    }
    if (type == ActionType::OverrideAnimationTiming) {
        return PhosphorI18n::tr("Uses a different animation duration for one event on matching windows.");
    }
    if (type == ActionType::OverrideAnimationCurve) {
        return PhosphorI18n::tr("Uses a different easing curve for one event on matching windows.");
    }
    if (type == ActionType::OverrideDecorationChain) {
        return PhosphorI18n::tr(
            "Replaces the decoration packs drawn on matching windows. An empty list removes their "
            "decorations.");
    }
    if (type == ActionType::SetOpacity) {
        return PhosphorI18n::tr(
            "Dims matching windows to the given opacity. It takes effect only while the opacity and "
            "tint layer is on for the window, either from the global setting or from a Show opacity "
            "and tint rule. A window with its own decoration chain dims through that chain's pack "
            "parameters instead.");
    }
    if (type == ActionType::SetOpacityTintVisible) {
        return PhosphorI18n::tr(
            "Turns the opacity and tint layer on or off for matching windows. The Set opacity and "
            "tint rules need this layer on to have any effect.");
    }
    if (type == ActionType::SetTintStrength) {
        return PhosphorI18n::tr(
            "Sets how strongly the tint colors matching windows. Takes effect only while the "
            "opacity and tint layer is on for the window.");
    }
    if (type == ActionType::SetTintColor) {
        return PhosphorI18n::tr(
            "Sets the tint color for matching windows. Takes effect only while the opacity and "
            "tint layer is on for the window.");
    }
    // ── Border and title bar (window) ──
    if (type == ActionType::SetHideTitleBar) {
        return PhosphorI18n::tr(
            "Hides the title bar on matching windows, or forces it visible where a mode would "
            "hide it.");
    }
    if (type == ActionType::SetBorderVisible) {
        return PhosphorI18n::tr("Shows or hides the PlasmaZones border on matching windows.");
    }
    if (type == ActionType::SetBorderWidth) {
        return PhosphorI18n::tr("Sets the border thickness on matching windows.");
    }
    if (type == ActionType::SetBorderRadius) {
        return PhosphorI18n::tr("Sets the border corner radius on matching windows.");
    }
    if (type == ActionType::SetBorderColorActive) {
        return PhosphorI18n::tr("Sets the border color for matching windows while they are focused.");
    }
    if (type == ActionType::SetBorderColorInactive) {
        return PhosphorI18n::tr("Sets the border color for matching windows while they are not focused.");
    }
    // ── Overlay (context) ──
    if (type == ActionType::OverrideOverlayShader) {
        return PhosphorI18n::tr("Uses a different zone overlay shader for the matched screen, desktop or activity.");
    }
    if (type == ActionType::OverrideOverlayStyle) {
        return PhosphorI18n::tr(
            "Switches the zone overlay between zone rectangles and the layout preview for the "
            "matched context.");
    }
    if (type == ActionType::SetOverlayHighlightColor) {
        return PhosphorI18n::tr("Sets the highlighted zone color of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayInactiveColor) {
        return PhosphorI18n::tr("Sets the inactive zone color of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayBorderColor) {
        return PhosphorI18n::tr("Sets the zone border color of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayActiveOpacity) {
        return PhosphorI18n::tr("Sets the highlighted zone opacity of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayInactiveOpacity) {
        return PhosphorI18n::tr("Sets the inactive zone opacity of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayBorderWidth) {
        return PhosphorI18n::tr("Sets the zone border thickness of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayBorderRadius) {
        return PhosphorI18n::tr("Sets the zone corner radius of the overlay for the matched context.");
    }
    if (type == ActionType::SetOverlayShowZoneNumbers) {
        return PhosphorI18n::tr("Shows or hides the zone numbers on the overlay for the matched context.");
    }
    // ── Gaps (context) ──
    if (type == ActionType::SetInnerGap) {
        return PhosphorI18n::tr("Sets the gap between zones for the matched screen, desktop or activity.");
    }
    if (type == ActionType::SetOuterGap) {
        return PhosphorI18n::tr("Sets the uniform gap around the screen edge for the matched context.");
    }
    if (type == ActionType::SetUsePerSideOuterGap) {
        return PhosphorI18n::tr("Switches the matched context between one uniform outer gap and per-side outer gaps.");
    }
    if (type == ActionType::SetOuterGapTop) {
        return PhosphorI18n::tr("Sets the top outer gap for the matched context.");
    }
    if (type == ActionType::SetOuterGapBottom) {
        return PhosphorI18n::tr("Sets the bottom outer gap for the matched context.");
    }
    if (type == ActionType::SetOuterGapLeft) {
        return PhosphorI18n::tr("Sets the left outer gap for the matched context.");
    }
    if (type == ActionType::SetOuterGapRight) {
        return PhosphorI18n::tr("Sets the right outer gap for the matched context.");
    }
    // ── Tiling parameters (context) ──
    if (type == ActionType::SetMaxWindows) {
        return PhosphorI18n::tr("Caps how many windows the tiling algorithm arranges on the matched context.");
    }
    if (type == ActionType::SetSplitRatio) {
        return PhosphorI18n::tr("Sets the master area split ratio for the matched context.");
    }
    if (type == ActionType::SetMasterCount) {
        return PhosphorI18n::tr("Sets how many windows the master area holds for the matched context.");
    }
    if (type == ActionType::SetInsertPosition) {
        return PhosphorI18n::tr("Chooses where newly opened windows enter the tiling stack for the matched context.");
    }
    if (type == ActionType::SetOverflowBehavior) {
        return PhosphorI18n::tr(
            "Chooses what happens to windows past the cap for the matched context. They can float, "
            "or the cap can be ignored.");
    }
    if (type == ActionType::SetDragBehavior) {
        return PhosphorI18n::tr(
            "Chooses whether dragging a tiled window floats it out or swaps it within the stack "
            "for the matched context.");
    }
    if (type == ActionType::SetAlgorithmParam) {
        return PhosphorI18n::tr(
            "Overrides a tiling algorithm's own parameters for the matched context. Applies only "
            "while that algorithm is active there.");
    }
    // ── Scrolling parameters (context) ──
    if (type == ActionType::SetScrollDefaultColumnWidth) {
        return PhosphorI18n::tr("Sets the width newly opened columns take on the matched scrolling context.");
    }
    if (type == ActionType::SetCenterFocusedColumn) {
        return PhosphorI18n::tr(
            "Chooses when the scrolling view re-centers on the focused column for the matched "
            "context.");
    }
    if (type == ActionType::SetScrollDefaultColumnDisplay) {
        return PhosphorI18n::tr("Chooses whether new columns open side by side or tabbed on the matched context.");
    }
    if (type == ActionType::SetScrollInsertPosition) {
        return PhosphorI18n::tr("Chooses where a new window's column enters the strip for the matched context.");
    }
    if (type == ActionType::SetScrollDefaultWindowHeight) {
        return PhosphorI18n::tr(
            "Sets the height newly opened windows take inside their column for the matched "
            "context.");
    }
    if (type == ActionType::SetScrollAlwaysCenterSingleColumn) {
        return PhosphorI18n::tr(
            "Centers a column on the matched context whenever it is the only one on the strip, "
            "whatever the centering policy says.");
    }
    if (type == ActionType::SetScrollRespectMinimumSize) {
        return PhosphorI18n::tr(
            "Keeps columns on the matched context at least as wide as each window asks to be. "
            "Off lets a column go narrower, which the compositor then clamps on its own.");
    }
    if (type == ActionType::SetScrollCropStraddlers) {
        return PhosphorI18n::tr(
            "Clips a column that hangs over the screen edge on the matched context so only the "
            "on-screen part is drawn. The column keeps its real size, so scrolling is unchanged.");
    }
    if (type == ActionType::SetScrollFocusNewWindows) {
        return PhosphorI18n::tr(
            "Gives windows opening on the matched context keyboard focus. A per-window Focus when "
            "opened rule still wins over this.");
    }
    if (type == ActionType::SetScrollSmartGaps) {
        return PhosphorI18n::tr(
            "Drops the outer gaps on the matched context while a single column is on the strip, "
            "so a lone window fills the screen.");
    }
    if (type == ActionType::SetScrollFocusFollowsMouse) {
        return PhosphorI18n::tr(
            "Focuses whichever column the pointer moves over on the matched context, without a "
            "click. Covers scrolling screens only, so snapping and tiling screens keep following "
            "the global setting.");
    }
    if (type == ActionType::SetScrollStickyWindowHandling) {
        return PhosphorI18n::tr(
            "Chooses how the matched context treats windows shown on all desktops. Anything other "
            "than treating them as normal keeps them floating instead of in a column.");
    }
    // ── Tab indicator (context + window colours) ──
    if (type == ActionType::SetTabIndicatorEnabled) {
        return PhosphorI18n::tr("Shows or hides the tab indicator on tabbed columns for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorStyle) {
        return PhosphorI18n::tr(
            "Switches the tab indicator between a segment bar and titled chips for the matched "
            "context.");
    }
    if (type == ActionType::SetTabIndicatorPosition) {
        return PhosphorI18n::tr("Chooses which column edge the tab indicator runs along for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorHideWhenSingleTab) {
        return PhosphorI18n::tr(
            "Hides the tab indicator when a tabbed column holds only one window, for the matched "
            "context.");
    }
    if (type == ActionType::SetTabIndicatorPlaceWithinColumn) {
        return PhosphorI18n::tr(
            "Reserves the tab indicator's space inside the column instead of drawing beside it, "
            "for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorGap) {
        return PhosphorI18n::tr(
            "Sets the gap between the tab indicator and the window for the matched context. A "
            "negative gap draws it over the window.");
    }
    if (type == ActionType::SetTabIndicatorWidth) {
        return PhosphorI18n::tr("Sets the tab indicator's thickness for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorLength) {
        return PhosphorI18n::tr("Sets how much of the column edge the tab indicator spans for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorGapsBetweenTabs) {
        return PhosphorI18n::tr("Sets the gap between individual tabs for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorCornerRadius) {
        return PhosphorI18n::tr("Sets the corner radius of each tab for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorActiveColor) {
        return PhosphorI18n::tr("Sets the active tab color for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorInactiveColor) {
        return PhosphorI18n::tr("Sets the inactive tab color for the matched context.");
    }
    if (type == ActionType::SetTabIndicatorUrgentColor) {
        return PhosphorI18n::tr("Sets the color of a tab whose window asks for attention, for the matched context.");
    }
    if (type == ActionType::TabColorActive) {
        return PhosphorI18n::tr("Recolors a matching window's own tab while it is the active one.");
    }
    if (type == ActionType::TabColorInactive) {
        return PhosphorI18n::tr("Recolors a matching window's own tab while another tab is active.");
    }
    if (type == ActionType::TabColorUrgent) {
        return PhosphorI18n::tr("Recolors a matching window's own tab while it asks for attention.");
    }
    // ── Drop indicator (context + window colours) ──
    if (type == ActionType::SetDropIndicatorEnabled) {
        return PhosphorI18n::tr("Shows or hides the drop indicator during drag re-inserts for the matched context.");
    }
    if (type == ActionType::SetDropIndicatorColor) {
        return PhosphorI18n::tr("Sets the drop indicator's fill color for the matched context.");
    }
    if (type == ActionType::SetDropIndicatorBorderColor) {
        return PhosphorI18n::tr("Sets the drop indicator's border color for the matched context.");
    }
    if (type == ActionType::SetDropIndicatorOpacity) {
        return PhosphorI18n::tr("Sets the drop indicator's fill opacity for the matched context.");
    }
    if (type == ActionType::SetDropIndicatorBorderWidth) {
        return PhosphorI18n::tr("Sets the drop indicator's border thickness for the matched context.");
    }
    if (type == ActionType::SetDropIndicatorBorderRadius) {
        return PhosphorI18n::tr("Sets the drop indicator's corner radius for the matched context.");
    }
    if (type == ActionType::DropIndicatorColor) {
        return PhosphorI18n::tr("Recolors the drop indicator's fill while a matching window is being dragged.");
    }
    if (type == ActionType::DropIndicatorBorderColor) {
        return PhosphorI18n::tr("Recolors the drop indicator's border while a matching window is being dragged.");
    }
    return QString();
}

} // namespace PlasmaZones::RuleAuthoring

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>

// The built-in SLOT ids — the conflict-resolution keys the evaluator fills
// (first-matching-rule-wins per slot). Split out of RuleAction.h purely for
// file size — that header includes this one, so every existing consumer of
// `PhosphorRules/RuleAction.h` keeps compiling unchanged.
//
// The companion splits are ActionTypes.h (action type ids) and ActionParams.h
// (param keys, validation bounds and the closed token vocabularies).

namespace PhosphorRules {

// ── Built-in slot ids ──
namespace ActionSlot {
inline constexpr QLatin1StringView EngineMode{"engine-mode"};
inline constexpr QLatin1StringView Layout{"layout"};
/// Context-domain scrolling-template slot — filled by
/// `ActionType::SetScrollingTemplate`. Its own slot (not `Layout`): the
/// lossless assignment set can carry a snapping layout AND a scrolling
/// template in one rule, and per-slot accumulation would drop one of them.
inline constexpr QLatin1StringView ScrollingTemplate{"scrolling-template"};
inline constexpr QLatin1StringView EngineEnable{"engine-enable"};
/// Context-domain layout-lock slot — filled by `ActionType::LockContext`.
/// A single boolean: a winning rule with `value == true` locks the context.
///
/// NOTE, as for `DefaultAssignment` and `OsdEnabled` below: the slot id itself
/// has no reader. `LayoutRegistry::resolveContextLocked` scans the matched
/// rules for the action TYPE and reads its `value` param directly, so the id
/// exists to satisfy the registry invariant that every non-terminal descriptor
/// resolves a non-empty slot (`ActionRegistry::validate` rejects an action
/// whose `slotFor` returns empty) and to keep the action out of any other
/// action's slot. Same situation the `AnimExclude` / `DecorationExclude` notes
/// at the end of this namespace describe.
inline constexpr QLatin1StringView Locked{"locked"};
/// Context-domain default-assignment override slot — filled by
/// `ActionType::DefaultLayoutAssignment`. A single boolean (first-matching-rule-
/// wins): `false` suppresses the synthesized level-1 default for the context,
/// `true` forces it through. Read at cascade-miss by
/// `LayoutRegistry::resolveContextDefaultAssignment`, which — like the
/// `Locked` resolver above — scans for the action TYPE rather than this slot
/// id; see that note for why the id still exists.
inline constexpr QLatin1StringView DefaultAssignment{"default-assignment"};
/// Context-domain OSD-visibility override slot — filled by
/// `ActionType::SetOsdEnabled`. A single boolean (first-matching-rule-wins):
/// `false` suppresses every OSD for the context, `true` forces them past the
/// per-trigger global toggles. Read by the daemon's OSD gates via
/// `LayoutRegistry::resolveContextOsdEnabled`, which — like the two resolvers
/// above — scans for the action TYPE rather than this slot id; see the
/// `Locked` note for why the id still exists.
inline constexpr QLatin1StringView OsdEnabled{"osd-enabled"};
/// Context-domain drag-selector-visibility override slot — filled by
/// `ActionType::SetDragSelectorEnabled`. A single boolean (first-matching-rule-
/// wins): `false` suppresses the drag selector popup for the context, `true`
/// forces it past the global selector toggle. Read by the drag adaptor's
/// selector gate via `LayoutRegistry::resolveContextDragSelectorEnabled`,
/// which — like the three resolvers above — scans for the action TYPE rather
/// than this slot id; see the `Locked` note for why the id still exists.
inline constexpr QLatin1StringView DragSelectorEnabled{"drag-selector-enabled"};
inline constexpr QLatin1StringView Manage{"manage"};
inline constexpr QLatin1StringView Float{"float"};
/// Window-scoped open-placement slot — filled by `ActionType::SnapToZone`. A
/// single slot (first-matching-rule-wins), carrying the zone-ordinal list
/// (`ActionParam::Zones`) the daemon snaps the opening window into. Mutually
/// resolved against `Float` on the open path (a float rule opts out of snapping).
inline constexpr QLatin1StringView Placement{"placement"};
/// Window-scoped open-routing slots — filled by `ActionType::RouteToScreen` /
/// `RouteToDesktop`. Each is a single slot (first-matching-rule-wins) carrying the
/// target monitor / desktop the daemon routes the opening window to. Independent
/// of `Placement`: a window can be routed to a screen AND snapped to a zone there,
/// or routed with no zone (just moved to the monitor / desktop).
inline constexpr QLatin1StringView RouteScreen{"route-screen"};
inline constexpr QLatin1StringView RouteDesktop{"route-desktop"};
inline constexpr QLatin1StringView Opacity{"opacity"};
inline constexpr QLatin1StringView RestorePosition{"restore-position"};
// Per-window restore-policy overrides (one slot each). Filled by
// SetRestoreToZoneOnLogin / SetRestoreSizeOnUnsnap, read daemon-side.
inline constexpr QLatin1StringView RestoreToZoneOnLogin{"restore-to-zone-on-login"};
inline constexpr QLatin1StringView RestoreSizeOnUnsnap{"restore-size-on-unsnap"};
inline constexpr QLatin1StringView UnfloatFallbackToZone{"unfloat-fallback-to-zone"};
/// Window-scoped stacking-layer slot — filled by `ActionType::SetWindowLayer`,
/// read by the KWin effect's reconcileRuleWindowLayer.
inline constexpr QLatin1StringView WindowLayer{"window-layer"};
// Per-window border / title-bar appearance slots (one per property so
// independent rules cascade per-property).
inline constexpr QLatin1StringView HideTitleBar{"hide-title-bar"};
inline constexpr QLatin1StringView BorderVisible{"border-visible"};
inline constexpr QLatin1StringView BorderWidth{"border-width"};
inline constexpr QLatin1StringView BorderRadius{"border-radius"};
inline constexpr QLatin1StringView BorderColorActive{"border-color-active"};
inline constexpr QLatin1StringView BorderColorInactive{"border-color-inactive"};
// Per-window opacity+tint layer slots (SetOpacityTintVisible /
// SetTintStrength / SetTintColor), feeding the plain opacity+tint layer's
// reserved pack the way the border slots feed "border".
inline constexpr QLatin1StringView OpacityTintVisible{"opacity-tint-visible"};
inline constexpr QLatin1StringView TintStrength{"tint-strength"};
inline constexpr QLatin1StringView TintColor{"tint-color"};
// Per-context gap slots (mirror the PerScreenKeys gap set).
inline constexpr QLatin1StringView InnerGap{"inner-gap"};
inline constexpr QLatin1StringView OuterGap{"outer-gap"};
inline constexpr QLatin1StringView UsePerSideOuterGap{"use-per-side-outer-gap"};
inline constexpr QLatin1StringView OuterGapTop{"outer-gap-top"};
inline constexpr QLatin1StringView OuterGapBottom{"outer-gap-bottom"};
inline constexpr QLatin1StringView OuterGapLeft{"outer-gap-left"};
inline constexpr QLatin1StringView OuterGapRight{"outer-gap-right"};
// Per-context autotile parameter slots (one per param). Filled by
// SetMaxWindows / SetSplitRatio / SetMasterCount / SetInsertPosition /
// SetOverflowBehavior / SetDragBehavior / SetAlgorithmParam, read by
// LayoutRegistry::resolveContextTilingParams and layered onto the per-screen
// autotile override map daemon-side (drag behavior via the drag adaptor;
// AlgorithmParams carries a target algorithm token plus a free-form params blob).
inline constexpr QLatin1StringView MaxWindows{"max-windows"};
inline constexpr QLatin1StringView SplitRatio{"split-ratio"};
inline constexpr QLatin1StringView MasterCount{"master-count"};
inline constexpr QLatin1StringView InsertPosition{"insert-position"};
inline constexpr QLatin1StringView OverflowBehavior{"overflow-behavior"};
inline constexpr QLatin1StringView DragBehavior{"drag-behavior"};
inline constexpr QLatin1StringView AlgorithmParams{"algorithm-params"};
// Per-context scrolling parameter slots (one per param). Filled by
// SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
// SetScrollDefaultColumnDisplay / SetScrollInsertPosition /
// SetScrollDefaultWindowHeight, read by
// LayoutRegistry::resolveContextScrollingParams and layered onto the scrolling
// engine's per-screen config the way the autotile params are.
inline constexpr QLatin1StringView ScrollDefaultColumnWidth{"scroll-default-column-width"};
inline constexpr QLatin1StringView CenterFocusedColumn{"center-focused-column"};
inline constexpr QLatin1StringView ScrollDefaultColumnDisplay{"scroll-default-column-display"};
inline constexpr QLatin1StringView ScrollInsertPosition{"scroll-insert-position"};
inline constexpr QLatin1StringView ScrollDefaultWindowHeight{"scroll-default-window-height"};
/// Per-context scrolling BEHAVIOUR slots — the toggles that had no rule seam
/// until now. The five below ride the same per-screen override map as the
/// sizing slots above, and the engine reads each through an `effective*`
/// accessor that falls back to the global config value; the sixth,
/// ScrollFocusFollowsMouse, is resolved per screen and pushed to the
/// compositor instead (see its own note).
inline constexpr QLatin1StringView ScrollAlwaysCenterSingleColumn{"scroll-always-center-single-column"};
inline constexpr QLatin1StringView ScrollRespectMinimumSize{"scroll-respect-minimum-size"};
inline constexpr QLatin1StringView ScrollCropStraddlers{"scroll-crop-straddlers"};
inline constexpr QLatin1StringView ScrollFocusNewWindows{"scroll-focus-new-windows"};
inline constexpr QLatin1StringView ScrollSmartGaps{"scroll-smart-gaps"};
/// Effect-consumed, unlike its five neighbours: the daemon resolves it per
/// screen and pushes the resolved set to the compositor.
inline constexpr QLatin1StringView ScrollFocusFollowsMouse{"scroll-focus-follows-mouse"};
inline constexpr QLatin1StringView ScrollStickyWindowHandling{"scroll-sticky-window-handling"};
// Per-context tab-indicator slots, one per property so independent context
// rules cascade per-property. Filled by the SetTabIndicator* actions and read
// by LayoutRegistry::resolveContextScrollingParams into ContextScrollingParams.
inline constexpr QLatin1StringView TabIndicatorEnabled{"tab-indicator-enabled"};
inline constexpr QLatin1StringView TabIndicatorStyle{"tab-indicator-style"};
inline constexpr QLatin1StringView TabIndicatorPosition{"tab-indicator-position"};
inline constexpr QLatin1StringView TabIndicatorHideWhenSingleTab{"tab-indicator-hide-when-single-tab"};
inline constexpr QLatin1StringView TabIndicatorPlaceWithinColumn{"tab-indicator-place-within-column"};
inline constexpr QLatin1StringView TabIndicatorGap{"tab-indicator-gap"};
inline constexpr QLatin1StringView TabIndicatorWidth{"tab-indicator-width"};
inline constexpr QLatin1StringView TabIndicatorLength{"tab-indicator-length"};
inline constexpr QLatin1StringView TabIndicatorGapsBetweenTabs{"tab-indicator-gaps-between-tabs"};
inline constexpr QLatin1StringView TabIndicatorCornerRadius{"tab-indicator-corner-radius"};
inline constexpr QLatin1StringView TabIndicatorActiveColor{"tab-indicator-active-color"};
inline constexpr QLatin1StringView TabIndicatorInactiveColor{"tab-indicator-inactive-color"};
inline constexpr QLatin1StringView TabIndicatorUrgentColor{"tab-indicator-urgent-color"};
// Per-context drop-indicator slots, one per property so independent context
// rules cascade per-property. Filled by the SetDropIndicator* actions and read
// by LayoutRegistry::resolveContextScrollingParams into ContextScrollingParams.
inline constexpr QLatin1StringView DropIndicatorEnabled{"drop-indicator-enabled"};
inline constexpr QLatin1StringView DropIndicatorColor{"drop-indicator-color"};
inline constexpr QLatin1StringView DropIndicatorBorderColor{"drop-indicator-border-color"};
inline constexpr QLatin1StringView DropIndicatorOpacity{"drop-indicator-opacity"};
inline constexpr QLatin1StringView DropIndicatorBorderWidth{"drop-indicator-border-width"};
inline constexpr QLatin1StringView DropIndicatorBorderRadius{"drop-indicator-border-radius"};
// Per-window scrolling open slots (one per property so independent rules
// cascade per-property). Filled by OpenColumnWidth / OpenTabbed /
// OpenColumnPlacement / OpenWindowHeight / OpenMaximized / OpenFocused, read
// on the open path by the scrolling engine.
//
// Two ids in this block are NOT scrolling-engine open slots and sit here only
// because their actions are authored alongside the family: OpenFullscreen is
// filled by an effect-consumed action the compositor reads at windowAdded (it
// applies on every engine mode), and ScrollFactor is not an open-path slot at
// all — it is a live per-window input multiplier the effect re-reads while the
// pointer hovers the window. Both carry Tag::EffectVerdict for that reason.
inline constexpr QLatin1StringView OpenColumnWidth{"open-column-width"};
inline constexpr QLatin1StringView OpenTabbed{"open-tabbed"};
/// Per-window tab-colour slots, filled by the TabColor* window actions and
/// resolved per tab when the daemon builds the tab-indicator model.
inline constexpr QLatin1StringView TabColorActive{"tab-color-active"};
inline constexpr QLatin1StringView TabColorInactive{"tab-color-inactive"};
inline constexpr QLatin1StringView TabColorUrgent{"tab-color-urgent"};
/// Per-window drop-indicator colour slots, filled by the DropIndicator*
/// window actions and resolved at drag start from the DRAGGED window's rules.
inline constexpr QLatin1StringView DragDropIndicatorColor{"drag-drop-indicator-color"};
inline constexpr QLatin1StringView DragDropIndicatorBorderColor{"drag-drop-indicator-border-color"};
inline constexpr QLatin1StringView OpenColumnPlacement{"open-column-placement"};
inline constexpr QLatin1StringView OpenWindowHeight{"open-window-height"};
inline constexpr QLatin1StringView OpenMaximized{"open-maximized"};
inline constexpr QLatin1StringView OpenFocused{"open-focused"};
inline constexpr QLatin1StringView OpenFullscreen{"open-fullscreen"};
inline constexpr QLatin1StringView ScrollFactor{"scroll-factor"};
// Per-context overlay-property slots (one per property so independent rules
// cascade per-property). Filled by the OverrideOverlay* context actions, read
// by `LayoutRegistry::resolveContextOverlay`. OverlayShader carries the shader
// effect id (ActionParam::EffectId); OverlayStyle carries a wire token
// (ActionParam::Value).
inline constexpr QLatin1StringView OverlayShader{"overlay-shader"};
inline constexpr QLatin1StringView OverlayStyle{"overlay-style"};
// Per-context overlay-APPEARANCE slots (one per property so independent rules
// cascade per-property). Filled by the SetOverlay* appearance context actions,
// read by `LayoutRegistry::resolveContextOverlay` into ContextOverlayOverride.
inline constexpr QLatin1StringView OverlayHighlightColor{"overlay-highlight-color"};
inline constexpr QLatin1StringView OverlayInactiveColor{"overlay-inactive-color"};
inline constexpr QLatin1StringView OverlayBorderColor{"overlay-border-color"};
inline constexpr QLatin1StringView OverlayActiveOpacity{"overlay-active-opacity"};
inline constexpr QLatin1StringView OverlayInactiveOpacity{"overlay-inactive-opacity"};
inline constexpr QLatin1StringView OverlayBorderWidth{"overlay-border-width"};
inline constexpr QLatin1StringView OverlayBorderRadius{"overlay-border-radius"};
inline constexpr QLatin1StringView OverlayShowZoneNumbers{"overlay-show-zone-numbers"};
// Animation slots are event-scoped: "anim-shader:<event>" / "anim-timing:<event>"
// / "anim-curve:<event>". Curve and timing are split so they can be overridden
// independently per event — `resolveAnimationMotionProfile` reads the curve
// slot first and falls back to the timing slot's curve field for legacy rules.
inline constexpr QLatin1StringView AnimShaderPrefix{"anim-shader:"};
inline constexpr QLatin1StringView AnimTimingPrefix{"anim-timing:"};
inline constexpr QLatin1StringView AnimCurvePrefix{"anim-curve:"};
// Window-scoped decoration-chain override. Un-scoped (no event dimension —
// decoration is persistent state), so the highest-priority matching
// OverrideDecorationChain rule wins the whole slot. Read by the effect's
// updateWindowDecoration in place of the DecorationProfileTree user packs.
inline constexpr QLatin1StringView DecorationChain{"decoration-chain"};
/// Window-scoped, event-agnostic. Declared for ActionDescriptor
/// completeness — ExcludeAnimations carries `.slotFor =
/// constantSlot(ActionSlot::AnimExclude)`. NOT actually filled at
/// resolve time: ExcludeAnimations is `.terminal = true`, so
/// `RuleEvaluator::resolve` calls `markExcluded()` and breaks
/// BEFORE `fillSlot()` runs. The effect's `shouldAnimateWindow`
/// gates on `ResolvedActions::isExcluded()` (the dedicated
/// `m_animationExclusionEvaluator`), never on `hasSlot("anim-exclude")`
/// — so no consumer queries this slot id at runtime. Kept to satisfy
/// the action-registry invariant that every non-terminal slot id is
/// referenced; a future change that makes ExcludeAnimations non-
/// terminal (e.g. composing with override actions) would start
/// filling the slot, so the id stays load-bearing for that path.
inline constexpr QLatin1StringView AnimExclude{"anim-exclude"};
/// Window-scoped. Declared for ActionDescriptor completeness the same way
/// AnimExclude is — ExcludeDecorations is `.terminal = true`, so
/// `RuleEvaluator::resolve` calls `markExcluded()` and breaks before
/// `fillSlot()` runs. The effect's `shouldDecorateWindow` gates on
/// `ResolvedActions::isExcluded()` over the dedicated decoration-exclusion
/// evaluator, never on this slot id.
inline constexpr QLatin1StringView DecorationExclude{"decoration-exclude"};
} // namespace ActionSlot

} // namespace PhosphorRules

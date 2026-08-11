// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>

// Canonical wire strings for the built-in action TYPES, split out of
// RuleAction.h purely for file size — that header keeps the structural
// machinery (RuleAction, ParamSchema, ActionDescriptor, ActionRegistry, the
// Tag vocabulary) and includes this one, so every existing consumer of
// `PhosphorRules/RuleAction.h` keeps compiling unchanged and no include
// anywhere needs updating. Include this header directly only when a
// translation unit wants the type ids alone.
//
// The companion splits are ActionParams.h (param keys, validation bounds and
// the closed token vocabularies) and ActionSlots.h (slot ids).

namespace PhosphorRules {

// ── Built-in action type ids — canonical wire strings ──
namespace ActionType {
inline constexpr QLatin1StringView SetEngineMode{"setEngineMode"};
inline constexpr QLatin1StringView SetSnappingLayout{"setSnappingLayout"};
inline constexpr QLatin1StringView SetTilingAlgorithm{"setTilingAlgorithm"};
/// Scrolling-mode template for the matched context: a NATIVE
/// ScrollingTemplate id (its own picker kind, `scrollingTemplate`, with its
/// own name resolution — not a manual layout). It shares the LayoutId wire
/// KEY with SetSnappingLayout but the two id namespaces are disjoint, and it
/// fills its own cascade slot — the lossless mode-toggle contract stores it
/// BESIDE the snapping layout in one rule, and sharing the layout slot would
/// shadow one of the pair.
inline constexpr QLatin1StringView SetScrollingTemplate{"setScrollingTemplate"};
inline constexpr QLatin1StringView DisableEngine{"disableEngine"};
/// Lock the active layout for the matched screen/desktop/activity context so
/// it can't be switched — the rule-driven equivalent of the manual
/// ToggleLayoutLock shortcut. Context domain (matches only context fields);
/// mode-agnostic (a `true` lock applies to both the snapping and tiling
/// engines). The daemon resolves it LIVE on the context-lock path and never
/// persists it, so rule locks and manual toggles never overwrite each other.
inline constexpr QLatin1StringView LockContext{"lockContext"};
/// Per-context override of the global "suppress default layout assignment"
/// setting for the matched screen/desktop/activity context. Context domain
/// (matches only context fields); mode-agnostic (the override governs the
/// synthesized default for BOTH the snapping and tiling engines, since the
/// level-1 default is a single mode-carrying `AssignmentEntry`). Boolean
/// `value`: `false` SUPPRESSES the synthesized default for this context (no
/// engine activates until the user explicitly assigns one), `true` ALLOWS it
/// (forces the global default through even when the global suppress setting is
/// on). With no such rule, the context follows the global setting. Carries no
/// `SetEngineMode` action, so it is NOT a cascade-winning assignment rule — the
/// daemon reads it as a per-slot overlay at cascade-miss via
/// `LayoutRegistry::resolveContextDefaultAssignment`, mirroring `LockContext`.
inline constexpr QLatin1StringView DefaultLayoutAssignment{"defaultLayoutAssignment"};
/// Per-context override of the OSD toggles for the matched screen/desktop/
/// activity context. Context domain; boolean `value`: false SUPPRESSES every
/// on-screen display for the context, true FORCES them past the per-trigger
/// global toggles (but never past the OsdStyle::None kill switch, which
/// returns before a rendering style is chosen). With no such rule the context
/// follows the global toggles. Live-resolved daemon-side via
/// `LayoutRegistry::resolveContextOsdEnabled`, mirroring `LockContext`.
inline constexpr QLatin1StringView SetOsdEnabled{"setOsdEnabled"};
inline constexpr QLatin1StringView Exclude{"exclude"};
/// Exclude a matched window from the placement engines ONLY — snapping,
/// autotile and scrolling all treat it as unmanaged (open path, drag gate,
/// keyboard navigation), so it stays floating — while decorations and
/// animations still apply. The scoped sibling of the blanket `Exclude`,
/// which additionally strips decorations. Terminal like `Exclude`, and
/// sliced into the same placement-exclusion set consumers bind
/// (`ExclusionRules::excludePlacementRulesFrom` returns Exclude ∪
/// ExcludePlacement rules). Distinct from `Float`, which is an open-path
/// placement verdict on a MANAGED window (a floated window can still be
/// dragged into a zone; an excluded one cannot).
///
/// Scope enforcement: evaluators bound to the FULL store declare a
/// terminal-action scope (RuleEvaluator::setTerminalActionScope) so this
/// action terminates only placement-policy walks — on a mixed rule, its
/// presence does not cancel animation/appearance or context resolution.
/// Carries no tags, deliberately: no in-tree tag reader consumes the
/// exclusion family, and its placement effect flows exclusively through
/// the slicer.
inline constexpr QLatin1StringView ExcludePlacement{"excludePlacement"};
inline constexpr QLatin1StringView Float{"float"};
/// Snap a matched window into one or more zones on open. Carries a non-empty
/// list of 1-based zone ordinals (`ActionParam::Zones`); a single ordinal snaps
/// to that zone, multiple ordinals snap to their unioned bounding rect (zone
/// spanning). Ordinals are layout-agnostic — they address "zone N of whatever
/// layout is active on the window's screen", matching the snapToZone1..9
/// shortcuts. Daemon-consumed (placement) on the SnapEngine open path; supersedes
/// the retired per-layout `Layout::appRules`. Domain Window.
inline constexpr QLatin1StringView SnapToZone{"snapToZone"};
/// Route a matched window to a specific monitor on open. Carries the canonical
/// target screen id (`ActionParam::TargetScreenId`, the EDID `Manuf:Model:Serial`
/// form the settings screen-picker and the runtime both use — physical OR virtual
/// screen id). Composes with `SnapToZone` (snap into a zone ON the target screen)
/// and with autotile (insert the window into the target screen's tiling state); on
/// its own it just moves the window to that monitor. Restores the per-monitor
/// pinning the retired per-layout `Layout::appRules` `targetScreen` field carried.
/// Daemon-consumed on the open path. Domain Window.
inline constexpr QLatin1StringView RouteToScreen{"routeToScreen"};
/// Route a matched window to a specific virtual desktop on open. Carries the
/// 1-based desktop number (`ActionParam::TargetDesktop`). Composes with the other
/// open-path placement actions. Sticky (on-all-desktops) windows are left alone.
/// Daemon-consumed on the open path. Domain Window.
inline constexpr QLatin1StringView RouteToDesktop{"routeToDesktop"};
inline constexpr QLatin1StringView OverrideAnimationShader{"overrideAnimationShader"};
/// Per-window override of the decoration surface-pack chain (border-sweep /
/// glow / frosted-glass, ...). Carries an ordered pack-id array
/// (`ActionParam::Chain`) plus an optional per-pack parameter map
/// (`ActionParam::Params`, shape `{packId: {paramId: value}}`). An EMPTY
/// chain array is the "no decoration" sentinel, blocking the tree-resolved
/// chain for matched windows — the decoration analogue of
/// OverrideAnimationShader's empty effectId. The reserved rule-owned
/// "border" pack id is ignored if present (SetBorderVisible governs it).
/// Effect-consumed in updateWindowDecoration, replacing the
/// DecorationProfileTree user packs; one un-scoped slot, so the highest
/// priority matching rule wins outright. Domain Window.
inline constexpr QLatin1StringView OverrideDecorationChain{"overrideDecorationChain"};
inline constexpr QLatin1StringView OverrideAnimationTiming{"overrideAnimationTiming"};
/// Curve-only animation override — separate slot from timing so a user can
/// override the easing/spring curve for an event without committing to a
/// duration (and vice versa). `OverrideAnimationTiming` still carries both
/// for backward compatibility with legacy rules; `resolveAnimationMotionProfile`
/// checks the curve slot first.
inline constexpr QLatin1StringView OverrideAnimationCurve{"overrideAnimationCurve"};
inline constexpr QLatin1StringView SetOpacity{"setOpacity"};
/// Context-domain overlay-property overrides. A matched context rule
/// (screen / desktop / activity) overrides the active layout's overlay shader
/// or style (display mode: zone rectangles vs layout preview) for that context's
/// zone overlay. Resolved daemon-side via `LayoutRegistry::resolveContextOverlay`.
inline constexpr QLatin1StringView OverrideOverlayShader{"overrideOverlayShader"};
inline constexpr QLatin1StringView OverrideOverlayStyle{"overrideOverlayStyle"};
/// Context-domain overrides of the active layout's zone-overlay APPEARANCE —
/// the colours, opacities, border dimensions and zone-number visibility that
/// the global `Snapping.Zones.*` config sets. Each is its own slot so
/// independent rules cascade per-property, mirroring the per-window border
/// family. A matched context rule (screen / desktop / activity) overrides the
/// corresponding global setting for that context's overlay; an unset property
/// falls through to the global config value (config stays authoritative — these
/// only layer on top). Resolved daemon-side via
/// `LayoutRegistry::resolveContextOverlay` and consumed by the overlay service.
/// Colours carry a `#AARRGGBB` hex (`ActionParam::Value`); opacities a [0,1]
/// double; widths/radii a number; show-zone-numbers a bool. Unlike the border
/// colour actions there is NO accent sentinel — the overlay consumer resolves
/// no token, so the value is always a concrete hex.
inline constexpr QLatin1StringView SetOverlayHighlightColor{"setOverlayHighlightColor"};
inline constexpr QLatin1StringView SetOverlayInactiveColor{"setOverlayInactiveColor"};
inline constexpr QLatin1StringView SetOverlayBorderColor{"setOverlayBorderColor"};
inline constexpr QLatin1StringView SetOverlayActiveOpacity{"setOverlayActiveOpacity"};
inline constexpr QLatin1StringView SetOverlayInactiveOpacity{"setOverlayInactiveOpacity"};
inline constexpr QLatin1StringView SetOverlayBorderWidth{"setOverlayBorderWidth"};
inline constexpr QLatin1StringView SetOverlayBorderRadius{"setOverlayBorderRadius"};
inline constexpr QLatin1StringView SetOverlayShowZoneNumbers{"setOverlayShowZoneNumbers"};
/// Disable every animation override on a matched window. The opposite of
/// the OverrideAnimation* family — the effect's shouldAnimateWindow gate
/// surfaces this as "no animation for this window, regardless of other
/// rules". Distinct from the generic `Exclude` action (which marks the
/// window unmanaged by snap/tile/etc); a user can have `Exclude` without
/// `ExcludeAnimations` and vice versa. Migrated from the legacy
/// animationExcludedApplications / animationExcludedWindowClasses
/// settings lists by the v3→v4 chain.
inline constexpr QLatin1StringView ExcludeAnimations{"excludeAnimations"};

/// Disable window decorations (the border + surface-pack chain) on a matched
/// window — the decoration mirror of `ExcludeAnimations`. The KWin effect's
/// `shouldDecorateWindow` gate binds the decoration-exclusion slice
/// (`ExclusionRules::excludeDecorationsRulesFrom`, Exclude ∪
/// ExcludeDecorations), so the blanket `Exclude` keeps stripping decorations
/// while this action strips ONLY decorations — placement and animations are
/// untouched (full-store evaluators enforce this by scoping which terminal
/// actions they honour, see RuleEvaluator::setTerminalActionScope, so a
/// mixed rule carrying this action cannot cancel placement or animation
/// resolution). Like ExcludeAnimations it deliberately omits `Tag::Effect`:
/// carrying it would admit the rule into the effect's animation rule set,
/// whose "any match force-animates" opt-in gate must not fire for a
/// decoration opt-out. Carries `Tag::Border` as classification only — no
/// in-tree tag reader consumes it, and blanket `Exclude`'s decoration
/// effect flows exclusively through the slicer, never through tags.
/// Terminal.
inline constexpr QLatin1StringView ExcludeDecorations{"excludeDecorations"};

/// Per-window override for floated-position restore on login. A boolean `value`
/// action: true forces the window's previous floated position (and original
/// monitor) to be restored, false suppresses it. Engine-neutral — overrides the
/// per-engine `snappingRestoreFloatedWindowsOnLogin` /
/// `autotileRestoreFloatedWindowsOnLogin` / `scrollingRestoreFloatedWindowsOnLogin`
/// settings for matched windows. Resolved by the daemon-injected
/// restore-position predicate, which all THREE placement engines take:
/// consulted inside SnapEngine::resolveWindowRestore,
/// AutotileEngine::insertWindow and the scroll engine's floating-reopen branch.
/// Domain Window (matches window properties).
inline constexpr QLatin1StringView RestorePosition{"restorePosition"};

/// Per-window override for the "restore snapped windows to their zone on login"
/// setting. A boolean `value`: false suppresses zone restore for the matched
/// window (it reopens wherever the session put it), true forces it on even when
/// the global `restoreWindowsToZonesOnLogin` setting is off. Resolved by the
/// daemon-injected managed-restore predicate. Domain Window. The snapped-to-zone
/// analogue of RestorePosition (which covers FLOATED windows).
inline constexpr QLatin1StringView SetRestoreToZoneOnLogin{"setRestoreToZoneOnLogin"};

/// Per-window override for the "restore original size when unsnapped" setting. A
/// boolean `value`: false suppresses the pre-snap size restore for the matched
/// window (it keeps the zone size after unsnap), true forces it on even when the
/// global `restoreOriginalSizeOnUnsnap` setting is off. Consulted daemon-side on
/// the drag-out / drop / cursor-left-zones unsnap paths. Domain Window.
inline constexpr QLatin1StringView SetRestoreSizeOnUnsnap{"setRestoreSizeOnUnsnap"};

/// Per-window override of the "unfloat falls back to a zone" setting. A
/// boolean `value`: true places a matched window into a zone when it is
/// unfloated WITHOUT a remembered pre-float zone (last-used, else first
/// empty, else first zone), even when the global `snapUnfloatFallbackToZone`
/// setting is off; false suppresses the fallback so the window stays
/// floating. Resolved mid-session by the daemon-injected unfloat-fallback
/// predicate inside SnapEngine::resolveFallbackUnfloatGeometry, covering the
/// live unfloat and the SnapAdaptor restore-calculation twin. Domain Window.
inline constexpr QLatin1StringView SetUnfloatFallbackToZone{"setUnfloatFallbackToZone"};

/// Per-window stacking-layer override. Carries a closed enum token
/// (`ActionParam::Value`, WindowLayerToken): `above` keeps the matched window
/// above normal windows, `below` keeps it below, `normal` pins both flags off.
/// Effect-consumed (reconcileRuleWindowLayer): the token maps onto KWin's
/// keepAbove/keepBelow pair, BOTH flags are always written from the token (so
/// a layer flip can never leave a stale opposite flag), the window's pre-rule
/// flags are snapshotted on first application, and they are restored when no
/// rule owns the layer any more. Combined with the IsFloating / IsTiled match
/// fields this yields Krohnkite-style window layers ("floating windows above
/// tiled windows"). Domain Window.
inline constexpr QLatin1StringView SetWindowLayer{"setWindowLayer"};

// ── Per-window border / title-bar appearance overrides (domain Window) ──
// Effect-side per-window overrides of the global snap appearance. Each is its
// own slot so independent rules cascade per-property (a width rule and a
// colour rule on separate rules both apply). Applied to ANY matched window
// (snapped or floating), mirroring SetOpacity.
inline constexpr QLatin1StringView SetHideTitleBar{"setHideTitleBar"};
inline constexpr QLatin1StringView SetBorderVisible{"setBorderVisible"};
inline constexpr QLatin1StringView SetBorderWidth{"setBorderWidth"};
inline constexpr QLatin1StringView SetBorderRadius{"setBorderRadius"};
// Two single-colour border actions, one per focus state, each its own slot so
// independent rules cascade per-state. Each carries a single colour param
// (`ActionParam::Value`): a hex string OR the `BorderColorToken::Accent`
// sentinel. The effect's updateWindowDecoration reads the focused colour from
// SetBorderColorActive and the unfocused colour from SetBorderColorInactive;
// when the inactive action is absent the active colour is mirrored. The
// internal active/inactive naming matches KWin and the effect's
// activeColor/inactiveColor; the user-facing labels say focused/unfocused.
inline constexpr QLatin1StringView SetBorderColorActive{"setBorderColorActive"};
inline constexpr QLatin1StringView SetBorderColorInactive{"setBorderColorInactive"};

// ── Per-window opacity+tint layer overrides (domain Window) ──
// Effect-side per-window overrides of the plain opacity+tint layer, folded
// into the reserved "opacity-tint" surface pack exactly like the SetBorder*
// family feeds the "border" pack (each its own slot so independent rules
// cascade per-property). The layer's opacity keeps the existing `SetOpacity`
// slot and is layer-backed, full stop: when the layer renders, its value
// folds into the pack's opacity param (rule wins over the config value).
// Custom chains do not honour it — packs dim through their own parameters
// (frost/glass `contentOpacity`) — and neither does an undecorated window.
// SetTintColor carries a single colour param (`ActionParam::Value`): a hex
// string OR the `BorderColorToken::Accent` sentinel, resolved to the live
// system accent like the border colours.
inline constexpr QLatin1StringView SetOpacityTintVisible{"setOpacityTintVisible"};
inline constexpr QLatin1StringView SetTintStrength{"setTintStrength"};
inline constexpr QLatin1StringView SetTintColor{"setTintColor"};

// ── Per-context gap overrides (domain Context) ──
// Resolved daemon-side at zone-geometry time as the highest-precedence gap
// layer (rule > per-screen > layout > global). Match on screen / desktop /
// activity; per-property to mirror the PerScreenKeys gap set.
inline constexpr QLatin1StringView SetInnerGap{"setInnerGap"};
inline constexpr QLatin1StringView SetOuterGap{"setOuterGap"};
inline constexpr QLatin1StringView SetUsePerSideOuterGap{"setUsePerSideOuterGap"};
inline constexpr QLatin1StringView SetOuterGapTop{"setOuterGapTop"};
inline constexpr QLatin1StringView SetOuterGapBottom{"setOuterGapBottom"};
inline constexpr QLatin1StringView SetOuterGapLeft{"setOuterGapLeft"};
inline constexpr QLatin1StringView SetOuterGapRight{"setOuterGapRight"};

// ── Per-context autotile parameter overrides (domain Context) ──
// Override the global (or per-screen config) tiling parameters for the matched
// screen / desktop / activity. Layered ON TOP of config by the daemon when it
// builds the per-screen autotile override map (config stays authoritative; the
// rule wins where present). Each carries a single numeric `value`.
inline constexpr QLatin1StringView SetMaxWindows{"setMaxWindows"};
inline constexpr QLatin1StringView SetSplitRatio{"setSplitRatio"};
inline constexpr QLatin1StringView SetMasterCount{"setMasterCount"};
/// Where a newly-opened window is inserted into the autotile stack. Carries a
/// closed enum token (`ActionParam::Value`, InsertPositionToken). Context domain;
/// layered onto the per-screen override map like the other tiling params.
inline constexpr QLatin1StringView SetInsertPosition{"setInsertPosition"};
/// How the autotile stack handles windows beyond the max: float the overflow, or
/// go unlimited (ignore the cap). Closed enum token (OverflowBehaviorToken).
/// Context domain; layered onto the per-screen override map like the other params.
inline constexpr QLatin1StringView SetOverflowBehavior{"setOverflowBehavior"};
/// How dragging a tiled window behaves: float it out, or reorder it within the
/// stack (Krohnkite-style drag-to-swap). Closed enum token (DragBehaviorToken).
/// Context domain; consumed by the drag adaptor (NOT the tile-engine override
/// map) — it resolves the effective behavior for the drag's screen.
inline constexpr QLatin1StringView SetDragBehavior{"setDragBehavior"};
/// Override an autotile algorithm's custom (Luau-declared) parameters for the
/// matched context. Carries the target algorithm token (`ActionParam::Algorithm`)
/// and a free-form nested `params` object (`ActionParam::Params`) of the custom
/// parameter values — the same shape OverrideOverlayShader uses for shader
/// uniforms. Applied only when the target algorithm is the screen's effective
/// algorithm; layered over the global per-algorithm config. Context domain.
inline constexpr QLatin1StringView SetAlgorithmParam{"setAlgorithmParam"};

// ── Per-context scrolling parameter overrides (domain Context) ──
// Override the global (or per-screen config) scrolling-engine parameters for the
// matched screen / desktop / activity, the same way the autotile family above
// overrides the tiling params. Each carries a single `value`.
/// Width a newly-opened column takes, as a fraction of the work area. Carries a
/// numeric `ActionParam::Value` (the stored wire value is the fraction; the editor
/// shows a percent).
inline constexpr QLatin1StringView SetScrollDefaultColumnWidth{"setScrollDefaultColumnWidth"};
/// When the scroll viewport re-centres on the focused column. Closed enum token
/// (`ActionParam::Value`, CenterFocusedColumnToken).
inline constexpr QLatin1StringView SetCenterFocusedColumn{"setCenterFocusedColumn"};
/// How a newly-opened column displays its windows: side by side, or stacked as
/// tabs. Closed enum token (`ActionParam::Value`, ColumnDisplayToken).
inline constexpr QLatin1StringView SetScrollDefaultColumnDisplay{"setScrollDefaultColumnDisplay"};
/// Where a fresh-opened window's new column enters the strip. Closed enum
/// token (`ActionParam::Value`, ScrollInsertPositionToken).
inline constexpr QLatin1StringView SetScrollInsertPosition{"setScrollInsertPosition"};
/// Height a newly-opened window takes inside its column, as a fraction of the
/// work-area height (numeric `ActionParam::Value`, edited as a percent;
/// committed as a fixed pixel intent against the live work area).
inline constexpr QLatin1StringView SetScrollDefaultWindowHeight{"setScrollDefaultWindowHeight"};
/// Whether a lone column centres on the strip regardless of the centering
/// policy. Boolean `ActionParam::Value`. Rides the per-screen override map to
/// `ScrollLayoutParams::alwaysCenterSingleColumn`, beside CenterFocusedColumn.
inline constexpr QLatin1StringView SetScrollAlwaysCenterSingleColumn{"setScrollAlwaysCenterSingleColumn"};
/// Whether the strip honours each window's declared minimum size when sizing
/// its column. Boolean `ActionParam::Value`. Off lets a column go narrower
/// than the client asked for, which the compositor then clamps on commit.
inline constexpr QLatin1StringView SetScrollRespectMinimumSize{"setScrollRespectMinimumSize"};
/// Whether a column straddling the screen edge is clipped at the boundary
/// (its true rect is kept, only the drawing is cropped). Boolean
/// `ActionParam::Value`. Read by BOTH the engine's straddler clamp and the
/// compositor's paint clip, so the daemon pushes the resolved per-screen set
/// to the effect as well as onto the engine's override map.
inline constexpr QLatin1StringView SetScrollCropStraddlers{"setScrollCropStraddlers"};
/// Whether a window opening on the matched context takes focus. Boolean
/// `ActionParam::Value`. The per-window `OpenFocused` rule outranks this, and
/// this outranks the global focus-new-windows setting.
inline constexpr QLatin1StringView SetScrollFocusNewWindows{"setScrollFocusNewWindows"};
/// Whether a lone column drops the outer gaps (niri's smart gaps). Boolean
/// `ActionParam::Value`. The CONFIG value forwards from the tiling toggle
/// (the gap model is shared), but the rule slot is scrolling-only — a tiling
/// screen keeps reading its own config.
inline constexpr QLatin1StringView SetScrollSmartGaps{"setScrollSmartGaps"};
/// Whether moving the pointer over a column focuses it on the matched
/// context. Boolean `ActionParam::Value`. EFFECT-consumed: focus-follows-mouse
/// lives entirely in the compositor, so the daemon resolves this per screen
/// and pushes the resolved set over `org.plasmazones.Scrolling` rather than
/// onto the engine's override map. It governs the SCROLLING half of the
/// per-mode focus-follows-mouse split only — a snapping or tiling screen
/// keeps reading the global setting.
inline constexpr QLatin1StringView SetScrollFocusFollowsMouse{"setScrollFocusFollowsMouse"};
/// How the strip treats windows shown on all desktops. Closed enum token
/// (`ActionParam::Value`, StickyWindowHandlingToken). The scrolling engine
/// collapses both non-normal values to "float it", matching its single
/// consumption site.
inline constexpr QLatin1StringView SetScrollStickyWindowHandling{"setScrollStickyWindowHandling"};

// ── Per-context tab-indicator overrides (domain Context) ──
// niri's `tab-indicator` layout block, one action per property so independent
// context rules cascade per-property (a layout rule can set the position while
// a theme rule sets the colours, and neither clobbers the other). The GEOMETRY
// half reaches the scrolling engine through its per-screen override map; the
// PAINT half is consumed daemon-side and applied to the overlay, matching the
// split IScrollSettings documents.
/// Whether tabbed columns show an indicator at all. Boolean `ActionParam::Value`.
inline constexpr QLatin1StringView SetTabIndicatorEnabled{"setTabIndicatorEnabled"};
/// Title chips or a segment bar. Closed enum token (`ActionParam::Value`,
/// TabIndicatorStyleToken).
inline constexpr QLatin1StringView SetTabIndicatorStyle{"setTabIndicatorStyle"};
/// Which column edge the indicator runs along. Closed enum token
/// (`ActionParam::Value`, TabIndicatorPositionToken).
inline constexpr QLatin1StringView SetTabIndicatorPosition{"setTabIndicatorPosition"};
/// Hide the indicator on a single-window tabbed column. Boolean `ActionParam::Value`.
inline constexpr QLatin1StringView SetTabIndicatorHideWhenSingleTab{"setTabIndicatorHideWhenSingleTab"};
/// Reserve the indicator out of the column instead of drawing beside it.
/// Boolean `ActionParam::Value`.
inline constexpr QLatin1StringView SetTabIndicatorPlaceWithinColumn{"setTabIndicatorPlaceWithinColumn"};
/// Gap between indicator and window in px. Numeric `ActionParam::Value`, and
/// the one numeric action here whose range is SIGNED — a negative gap draws
/// the indicator over the window, which is niri's behaviour.
inline constexpr QLatin1StringView SetTabIndicatorGap{"setTabIndicatorGap"};
/// Indicator thickness in px. Numeric `ActionParam::Value`.
inline constexpr QLatin1StringView SetTabIndicatorWidth{"setTabIndicatorWidth"};
/// Indicator length as a fraction of the column extent. Numeric
/// `ActionParam::Value` (stored fraction, edited as a percent).
inline constexpr QLatin1StringView SetTabIndicatorLength{"setTabIndicatorLength"};
/// Gap between individual tabs in px. Numeric `ActionParam::Value`.
inline constexpr QLatin1StringView SetTabIndicatorGapsBetweenTabs{"setTabIndicatorGapsBetweenTabs"};
/// Per-tab corner radius in px. Numeric `ActionParam::Value`, signed like the
/// gap: -1 is the "fully rounded" sentinel the config layer uses.
inline constexpr QLatin1StringView SetTabIndicatorCornerRadius{"setTabIndicatorCornerRadius"};
/// Tab colours. Hex `ActionParam::Value`, same shapes as the border colours.
inline constexpr QLatin1StringView SetTabIndicatorActiveColor{"setTabIndicatorActiveColor"};
inline constexpr QLatin1StringView SetTabIndicatorInactiveColor{"setTabIndicatorInactiveColor"};
inline constexpr QLatin1StringView SetTabIndicatorUrgentColor{"setTabIndicatorUrgentColor"};

// ── Per-context drop-indicator overrides (domain Context) ──
// The drop-target highlight painted while a drag re-insert is armed. Context
// domain rather than Window for the same reason as the tab indicator's bulk:
// the indicator describes an empty SLOT on a screen, not a window, so a rule
// that matches a window has no referent for it. Every one of these is PAINT —
// unlike the tab indicator there is no geometry half, because the rect comes
// from the engine's own layout math and cannot be positioned independently of
// where the drop lands. They are therefore collected daemon-side and handed
// straight to the overlay, never through the engine's per-screen override map.
/// Whether the drop indicator is painted at all. Boolean `ActionParam::Value`.
inline constexpr QLatin1StringView SetDropIndicatorEnabled{"setDropIndicatorEnabled"};
/// Fill and border colours. Hex `ActionParam::Value`. EMPTY is not expressible
/// as a rule value — a rule that does not set the colour simply leaves the
/// setting (and through it the "follow the colour scheme" sentinel) in place.
inline constexpr QLatin1StringView SetDropIndicatorColor{"setDropIndicatorColor"};
inline constexpr QLatin1StringView SetDropIndicatorBorderColor{"setDropIndicatorBorderColor"};
/// Fill opacity, 0.0-1.0. Numeric `ActionParam::Value` (stored fraction,
/// edited as a percent). The border is always opaque, so this is the fill's
/// alone — see ScrollDropIndicatorContent.
inline constexpr QLatin1StringView SetDropIndicatorOpacity{"setDropIndicatorOpacity"};
/// Border thickness in px. Numeric `ActionParam::Value`. Zero is legal and
/// means a fill with no edge, so this floors at 0 rather than at 1.
inline constexpr QLatin1StringView SetDropIndicatorBorderWidth{"setDropIndicatorBorderWidth"};
/// Corner radius in px. Numeric `ActionParam::Value`. UNSIGNED, unlike the tab
/// indicator's: there is no pill sentinel here, 0 means square.
inline constexpr QLatin1StringView SetDropIndicatorBorderRadius{"setDropIndicatorBorderRadius"};

// ── Per-window scrolling open overrides (domain Window) ──
// Read on the open path for the matched window, layered over the context /
// config defaults above, so one application can open wide or tabbed without
// changing the engine's defaults.
/// Width the opening window's column takes, as a fraction of the work area.
/// Numeric `ActionParam::Value` (stored fraction, edited as a percent).
/// Ignored when OpenColumnPlacement resolves to "consume" on a non-empty
/// strip, because the arrival joins the host column and keeps its width.
inline constexpr QLatin1StringView OpenColumnWidth{"openColumnWidth"};
/// Whether the opening window's column starts tabbed. Boolean `ActionParam::Value`.
inline constexpr QLatin1StringView OpenTabbed{"openTabbed"};
/// Per-window tab colours — niri's `tab-indicator` WINDOW rule, which recolours
/// only the matched window's own tab. These outrank the per-context colours
/// above, which in turn outrank the config, which falls back to the theme:
/// niri's exact resolution order. Hex `ActionParam::Value`.
inline constexpr QLatin1StringView TabColorActive{"tabColorActive"};
inline constexpr QLatin1StringView TabColorInactive{"tabColorInactive"};
inline constexpr QLatin1StringView TabColorUrgent{"tabColorUrgent"};
/// Per-window drop-indicator colours, keyed on the DRAGGED window. The only
/// per-window slice of the drop indicator that has a coherent referent: while
/// a drag is in flight exactly one window is being dragged, so "show this
/// colour when dragging Firefox" resolves unambiguously at drag start. The
/// remaining four properties stay context-only — a per-window opacity or
/// border width would name the same slot the context rule already describes
/// with no added meaning. These outrank the per-context colours, which outrank
/// the config, which falls back to the theme: the tab colours' exact order.
/// Hex `ActionParam::Value`.
inline constexpr QLatin1StringView DropIndicatorColor{"dropIndicatorColor"};
inline constexpr QLatin1StringView DropIndicatorBorderColor{"dropIndicatorBorderColor"};
/// Whether the opening window starts its own column or is consumed into the
/// focused one. Closed enum token (`ActionParam::Value`, ColumnPlacementToken).
inline constexpr QLatin1StringView OpenColumnPlacement{"openColumnPlacement"};
/// Height the opening window takes inside its column, as a fraction of the
/// work-area height. Numeric `ActionParam::Value` (stored fraction, edited as
/// a percent). Applies after every insert path, outranking remembered and
/// default heights the way OpenColumnWidth outranks the width defaults.
inline constexpr QLatin1StringView OpenWindowHeight{"openWindowHeight"};
/// Whether the opening window's column starts maximized (full work-area
/// width) — niri's `open-maximized`. Boolean `ActionParam::Value`. Wins over
/// OpenColumnWidth when both match (a maximized open IS a width verdict, the
/// stronger one). There is no stored "maximized" state in the scroll engine —
/// full width is simply `ColumnWidth::makeProportion(1.0)` — so a later
/// un-maximize takes the default-width fallback, matching the manual verb.
inline constexpr QLatin1StringView OpenMaximized{"openMaximized"};
/// Per-window override of the "focus new windows" scrolling setting — niri's
/// `open-focused`. Boolean `ActionParam::Value`: true activates the window on
/// open even when the global setting is off, false keeps focus (and the
/// strip's active column) where it was. Read on the same open path as the
/// other Open* slots and layered over
/// `IScrollSettings::scrollingFocusNewWindows`.
inline constexpr QLatin1StringView OpenFocused{"openFocused"};
/// Fullscreen at open — niri's `open-fullscreen`. Boolean `ActionParam::Value`:
/// true puts the opening window into real KWin fullscreen, false vetoes the
/// app's OWN fullscreen request at open (apps that start fullscreen by
/// default). Unlike the rest of the Open* family this is EFFECT-consumed
/// (Tag::EffectVerdict, so an ExcludeAnimations rule cannot cancel it — see
/// the tag's doc): only the compositor side can flip KWin's fullscreen state,
/// and the flip happens at windowAdded, before the window is announced to the
/// daemon. One-shot open verdict — a rule edit mid-session does not yank an
/// already-open window into or out of fullscreen. Applies on every screen and
/// engine mode, not just scrolling.
inline constexpr QLatin1StringView OpenFullscreen{"openFullscreen"};
/// Per-window scroll-speed multiplier — niri's `scroll-factor` window rule.
/// Numeric `ActionParam::Value` in [MinScrollFactor, MaxScrollFactor],
/// multiplying every wheel / touchpad scroll delta delivered to the matched
/// window while the pointer is over it (below 1 slows scrolling, above 1
/// speeds it up). The wire value is the multiplier itself, a fraction below 1,
/// so the editor shows it as a percent (schema kind `percent`, scale 0.01) the
/// way the other fraction-valued actions do. EFFECT-consumed
/// (Tag::EffectVerdict, so an ExcludeAnimations rule cannot cancel it — see
/// the tag's doc): the KWin effect's input
/// filter rescales the axis event in place before the forwarding filter
/// hands it to the client. Wayland sessions only — on X11 the input filter
/// chain is not the client delivery path, so the rule is inert there.
inline constexpr QLatin1StringView ScrollFactor{"scrollFactor"};
} // namespace ActionType

} // namespace PhosphorRules

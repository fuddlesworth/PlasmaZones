// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>

// The action PAYLOAD vocabulary: param keys, the shared validation bounds, and
// the closed token sets a `value` param may carry. Split out of RuleAction.h
// purely for file size — that header includes this one, so every existing
// consumer of `PhosphorRules/RuleAction.h` keeps compiling unchanged.
//
// The bounds constants live here rather than in the private
// ruleaction_builtins_p.h because they are shared across library boundaries:
// the descriptor validators check them at load, and the daemon-side and
// compositor-side consumers re-check the same numbers on the way out, so a
// private copy in either would drift by hand-mirroring.
//
// The companion splits are ActionTypes.h (action type ids) and ActionSlots.h
// (slot ids).

namespace PhosphorRules {

// ── Action param keys — canonical wire strings ──
//
// Param-key vocabulary shared across every wire-shape reader (the registry
// validators in ruleaction_builtins_engine.cpp /
// ruleaction_builtins_appearance.cpp / ruleaction_builtins_indicators.cpp, the
// config-layer v3→v4 migration that ports legacy AnimationAppRule entries, the
// rule-editor UI, and the KWin-effect-side resolvers in
// `kwin-effect/plasmazoneseffect/shader_resolve.cpp`).
// A future rename (e.g. `effectId` → `effect_id`) updates one entry here and
// flows everywhere instead of being hard-coded once per reader.
namespace ActionParam {
// OverrideAnimation{Shader,Timing,Curve} family. `Params` is the exception:
// it is the shared nested-payload key for every action carrying a free-form
// uniform / parameter blob, so it is also read by SetOverlayShader,
// OverrideDecorationChain and SetAlgorithmParam.
inline constexpr QLatin1StringView Event{"event"};
inline constexpr QLatin1StringView EffectId{"effectId"};
inline constexpr QLatin1StringView Params{"params"};
inline constexpr QLatin1StringView Curve{"curve"};
inline constexpr QLatin1StringView DurationMs{"durationMs"};
// The shared SINGLE-PAYLOAD key: any action whose whole payload is one scalar
// stores it here, across the appearance, overlay, gap, engine-parameter and
// per-window-override families. The wire type follows the action's descriptor
// kind — a [0.0, 1.0] double for the opacity and fraction actions, an integer
// for the gap and border-metric actions, a bool for the on/off overrides, a
// `#AARRGGBB` hex string (or the `BorderColorToken::Accent` sentinel, resolved
// to the live system accent) for the colour actions, and an enum wire token for
// the token-valued ones.
inline constexpr QLatin1StringView Value{"value"};
// SetEngineMode / DisableEngine engine-token key — the wire token vocabulary
// is `PhosphorZones::modeToWireString(Mode)` (snapping / autotile / scrolling).
inline constexpr QLatin1StringView Mode{"mode"};
// SetSnappingLayout / SetScrollingTemplate layout-id key — wire is a
// `{uuid-with-braces}` string. The two id namespaces are disjoint:
// SetSnappingLayout carries a manual-layout uuid, SetScrollingTemplate a
// native scrolling-template uuid. The split is enforced at the CONSUMER, not
// at load, the same open-vocabulary shape SetEngineMode's `Mode` key uses: the
// descriptor validator only checks that the string is non-empty, so a layout
// uuid written into a SetScrollingTemplate action loads fine and then fails to
// resolve in `LayoutRegistry::scrollingTemplateForContext`, whose template-store
// lookup degrades an unknown id to "no template" and leaves the engine on its
// compiled defaults.
inline constexpr QLatin1StringView LayoutId{"layoutId"};
// SetTilingAlgorithm algorithm-token key — wire is the algorithm registry id.
inline constexpr QLatin1StringView Algorithm{"algorithm"};
// SnapToZone target-zone key — wire is a JSON array of 1-based zone ordinals
// (e.g. `[1]` or `[1, 2]`); multiple entries snap to their union. May be
// empty or absent when `ZoneNames` carries at least one name.
inline constexpr QLatin1StringView Zones{"zones"};
// SnapToZone target-zone-name key — wire is a JSON array of zone-name strings
// (e.g. `["Editor"]`). Names resolve against the zones of whatever layout is
// active on the placement screen, so a name-keyed rule follows the zone across
// layouts where an ordinal would not. Entries union with `Zones`; at least one
// of the two arrays must carry a valid entry.
inline constexpr QLatin1StringView ZoneNames{"zoneNames"};
// RouteToScreen target-monitor key — wire is the canonical EDID screen id
// (`Manuf:Model:Serial`, optionally `/CONNECTOR`-disambiguated, or a virtual
// screen id), the same form the ScreenId match field and the settings
// screen-picker store.
inline constexpr QLatin1StringView TargetScreenId{"targetScreenId"};
// RouteToDesktop target-desktop key — wire is a 1-based virtual desktop number.
inline constexpr QLatin1StringView TargetDesktop{"targetDesktop"};
// OverrideDecorationChain pack-list key — wire is a JSON array of surface-pack
// id strings, ordered as they fold. Empty array = "no decoration" sentinel.
// The per-pack parameter map rides the shared `Params` key as a nested object
// `{packId: {paramId: value}}`, mirroring OverrideAnimationShader's params.
inline constexpr QLatin1StringView Chain{"chain"};
} // namespace ActionParam

// Param-KIND vocabulary — the schema hint that tells the editor which widget to
// render, and tells a consumer what SHAPE a param is without knowing the action
// type. Only the kinds a consumer reads structurally are named here; the rest
// are spelled in their descriptors, which is where the editor reads them.
namespace ParamKind {
/// A param whose value is an animation event path (`window.appearance.open`).
/// Read by the KWin effect's window filter to tell an appearance action that
/// can fire from one pinned to an event no rule can drive.
inline constexpr QLatin1StringView AnimationEvent{"animationEvent"};
} // namespace ParamKind

/// Upper bound for a `SnapToZone` zone ordinal (each `ActionParam::Zones` entry).
/// No real layout has anywhere near this many zones (the snapToZone1..9 shortcuts
/// only reach 9); the cap exists purely to reject a grossly malformed hand-edited
/// payload AND to keep the load-time validator's integrality check from narrowing
/// an out-of-range double to int (which is UB). Shared by the descriptor validator
/// (ruleaction_builtins_engine.cpp), the daemon's placement reader
/// (windowtrackingadaptor/rules_placement.cpp `placementTargetsOf`), the
/// v3→v4 migration, and the settings editor (published to QML as the
/// `zoneOrdinals` ParamSchema `max`) so all of them stay in lockstep.
inline constexpr int MaxZoneOrdinal = 64;

/// Upper bound on a `SnapToZone` zone-name entry (each `ActionParam::ZoneNames`
/// string), in characters, measured on the TRIMMED name. This is deliberately
/// permissive: the layout editor caps zone names at 40 (MaxLayoutNameLength in
/// the app's constants.h, which this LGPL lib cannot include), but a hand-edited
/// or legacy layout may carry a longer one, so the bound here is only a
/// grossly-malformed-payload guard in the spirit of MaxZoneOrdinal, not the
/// editor's limit. Consumers: the descriptor validator, the daemon's placement
/// reader, and the settings authoring layer (parseZoneNameList and the
/// rule-list summary apply it in C++; it is also published as the `zoneNames`
/// ParamSchema `max` so the editor can read it from the schema).
inline constexpr int MaxZoneNameLength = 128;

/// Upper bound on a tab label font FAMILY (`SetTabIndicatorFontFamily`), in
/// characters, measured on the TRIMMED value. Same posture and same number as
/// MaxZoneNameLength: a grossly-malformed-payload guard, not a real limit —
/// installed family names run well under it, and the value normally arrives
/// from a QFontDatabase enumeration rather than being typed. It exists because
/// this is the one free-form string in the tab-indicator family, so without it
/// a hand-edited rules file could carry an unbounded one through the resolver
/// and onto the wire. EMPTY stays legal and is NOT a length failure: it is the
/// documented "use the system font" value. Consumers: the descriptor validator
/// (ruleaction_builtins_indicators.cpp), the settings-layer config validator
/// for both family keys (settingsschema_scrolling.cpp and settingsschema.cpp),
/// and the `value` ParamSchema `max`, which the rule editor's text field reads
/// so it cannot author a value the validator would then drop.
inline constexpr int MaxFontFamilyLength = 128;

/// Upper bounds for the per-window border appearance overrides
/// (`SetBorderWidth` / `SetBorderRadius`), in logical px. Shared so the
/// load-time descriptor validators (ruleaction_builtins_appearance.cpp for
/// the per-window pair, ruleaction_builtins_engine.cpp for the overlay
/// WIDTH — the overlay RADIUS deliberately uses its own wider
/// `kMaxOverlayBorderRadius`, see ruleaction_builtins_p.h) and the KWin-effect
/// consumer re-validation (shader_resolve.cpp) stay in lockstep — a
/// programmatically-built or hand-edited payload out of this range is
/// rejected at both boundaries rather than drawn.
inline constexpr double MaxBorderWidth = 10.0;
inline constexpr double MaxBorderRadius = 20.0;

/// Bounds for a scrolling-engine work-area FRACTION, width or height. The pair
/// is named for the column-width action it was introduced with, but it now
/// bounds four actions across both axes: `SetScrollDefaultColumnWidth` and
/// `OpenColumnWidth` against the work-area WIDTH, `SetScrollDefaultWindowHeight`
/// and `OpenWindowHeight` against its HEIGHT. One pair, because the axes share
/// the same "at least a sliver, at most the whole work area" policy. Shared for
/// the same lockstep reason as the border bounds above: the load-time
/// descriptor validators (via the private percent pair in
/// ruleaction_builtins_p.h), the zones-layer context resolver
/// (layoutregistry_contextparams.cpp), and the per-window open-params consumer
/// (windowtrackingadaptor/rules.cpp) all validate against these — a private
/// copy in any of them would drift by hand-mirroring. A column or window may
/// legitimately take the whole work area, so the upper bound is 1.0.
inline constexpr double MinColumnWidthRatio = 0.05;
inline constexpr double MaxColumnWidthRatio = 1.0;

/// Bounds for the focus-follows-mouse scroll cap, niri's
/// `max-scroll-amount`, as a FRACTION of the viewport's extent along the
/// strip. Installed here for the same lockstep reason as the pair above: the
/// descriptor validator and the zones-layer context resolver both check
/// against these.
///
/// The ceiling is 1.0 and it means "no cap", not an arbitrary limit: the
/// window under the pointer is by definition at least partly on screen, so
/// bringing it in never costs a full viewport of scroll. The floor is a real
/// setting rather than a disabled one, meaning focus only follows the pointer
/// onto windows already fully in view.
inline constexpr double MinFocusFollowsMouseMaxScrollRatio = 0.0;
inline constexpr double MaxFocusFollowsMouseMaxScrollRatio = 1.0;

/// Bounds for the tab-indicator numeric slots. Installed here, next to the
/// column-width pair and for the same reason: the descriptor validators
/// (ruleaction_builtins_indicators.cpp) and the per-context consumer
/// (layoutregistry_contextparams.cpp) both check against these, so a private
/// copy in either would drift by hand-mirroring.
///
/// Two floors are NEGATIVE and neither is a mistake. A negative GAP draws the
/// indicator on top of the window, which is niri's documented behaviour. The
/// corner-radius floor is the config layer's "fully rounded" SENTINEL, not a
/// real negative radius; the validators admit the whole [-1, max] range rather
/// than carving out (-1, 0), because every consumer rounds to an int and both
/// -1 and 0 are meaningful there.
///
/// MaxTabIndicatorGap bounds TWO slots: the indicator-to-window gap and the
/// between-tabs gap (which floors at 0 rather than at the negative). Moving it
/// moves both.
inline constexpr double MinTabIndicatorGap = -64.0;
inline constexpr double MaxTabIndicatorGap = 64.0;
inline constexpr double MinTabIndicatorWidth = 1.0;
inline constexpr double MaxTabIndicatorWidth = 64.0;
inline constexpr double TabIndicatorCornerRadiusPill = -1.0;
inline constexpr double MaxTabIndicatorCornerRadius = 64.0;
inline constexpr double MinTabIndicatorLengthRatio = 0.05;
inline constexpr double MaxTabIndicatorLengthRatio = 1.0;

/// Bounds for the tab label's font WEIGHT, the CSS/QFont::Weight scale where
/// 400 is regular and 700 is bold. Kept beside the other tab-indicator bounds
/// and shared by the same two consumers, so the descriptor validator and the
/// context resolver check one number rather than two hand-mirrored ones.
///
/// Inside this library it is one pair; outside it the SAME 100..900 band
/// is mirrored at three more sites, so widening it means editing four places.
/// The config layer owns the canonical pair
/// (ConfigDefaults::scrollingTabIndicatorFontWeight{Min,Max}() in
/// src/config/configdefaults_scrolling.h). The KWin effect mirrors it twice:
/// kFontWeightMin / kFontWeightMax in kwin-effect/tilinghandler/scrolltabs.cpp
/// name it, and the global loader in
/// kwin-effect/plasmazoneseffect/daemon_settings_scrolltabs.cpp spells it as
/// bare literals.
///
/// The floor is 100, not 0 — a zero weight is not a lighter font, it is an
/// out-of-scale value the font stack rounds to whatever it likes. So this pair
/// takes the EXPLICIT-FLOOR validator helper, like the signed bounds above,
/// even though both ends are positive.
///
/// The family beside it has no bounds constant because it is a free string,
/// and the three style FLAGS have none because they are booleans. There is no
/// font-SIZE pair at all: the painter fits the label to the pill thickness.
inline constexpr double MinTabIndicatorFontWeight = 100.0;
inline constexpr double MaxTabIndicatorFontWeight = 900.0;

/// Drop-indicator numeric bounds, mirroring the config layer's
/// (ConfigDefaults::scrollingDropIndicator*Min/Max) so a rule cannot author a
/// value the settings page would refuse. Both floors are 0 and neither is a
/// sentinel: a zero border width is a fill with no edge, and a zero radius is
/// a square corner. Deliberately NOT shared with the tab-indicator constants
/// above — the two families' ranges agree today by coincidence of taste, not
/// by contract, and tying them would make retuning one silently move the other.
inline constexpr double MinDropIndicatorOpacity = 0.0;
inline constexpr double MaxDropIndicatorOpacity = 1.0;
inline constexpr double MinDropIndicatorBorderWidth = 0.0;
inline constexpr double MaxDropIndicatorBorderWidth = 10.0;
inline constexpr double MinDropIndicatorBorderRadius = 0.0;
inline constexpr double MaxDropIndicatorBorderRadius = 50.0;

/// Bounds for the ScrollFactor multiplier. Shared by the load-time descriptor
/// validator (ruleaction_builtins_appearance.cpp) and the KWin-effect consumer
/// re-validation (shader_resolve.cpp's resolveScrollFactor) so the two stay in
/// lockstep — the usual reject-at-both-boundaries stance. The floor mirrors
/// niri's practical range (a factor below 1/20th makes scrolling read as
/// broken); the ceiling keeps a hand-edited payload from turning one wheel
/// notch into a page-length jump.
inline constexpr double MinScrollFactor = 0.05;
inline constexpr double MaxScrollFactor = 10.0;

/// Upper bound for a `RouteToDesktop` 1-based virtual-desktop number. KWin tops
/// out far below this in practice; the cap exists only to reject a grossly
/// malformed hand-edited payload and to keep the validator's integrality check
/// from narrowing an out-of-range double to int (UB). The descriptor validator
/// (ruleaction_builtins_engine.cpp) enforces the bound once, at load; downstream
/// consumers re-check the 1-based lower bound (and the label layer re-checks
/// this ceiling defensively), trusting the load-time clamp for the rest.
inline constexpr int MaxVirtualDesktopOrdinal = 1024;

/// Wire tokens for OverrideOverlayStyle's `value` param — the closed vocabulary
/// the descriptor's validator + `enumWireValues`, the daemon consumer
/// (`LayoutRegistry::resolveContextOverlay`), and the settings label layers
/// (`enumOptionLabel` + the rule-list summary) all read from this single source,
/// so a future rename can never desync the consumers. Mirrors how
/// `engineModeOptions()` centralizes the engine-mode vocabulary.
namespace OverlayStyleToken {
inline constexpr QLatin1StringView Rectangles{"rectangles"}; ///< OverlayDisplayMode::ZoneRectangles (0)
inline constexpr QLatin1StringView Preview{"preview"}; ///< OverlayDisplayMode::LayoutPreview (1)
} // namespace OverlayStyleToken

/// Wire tokens for SetInsertPosition's `value` param — the closed vocabulary the
/// descriptor validator, the daemon consumer (LayoutRegistry::resolveContextTilingParams
/// maps token → the AutotileInsertPosition int), and the settings label layers all
/// read from this single source. Ints match PhosphorTiles::AutotileInsertPosition.
namespace InsertPositionToken {
inline constexpr QLatin1StringView End{"end"}; ///< AutotileInsertPosition::End (0)
inline constexpr QLatin1StringView AfterFocused{"afterFocused"}; ///< AfterFocused (1)
inline constexpr QLatin1StringView AsMaster{"asMaster"}; ///< AsMaster (2)
} // namespace InsertPositionToken

/// Wire tokens for SetOverflowBehavior's `value` param. Ints match
/// PhosphorTiles::AutotileOverflowBehavior (Float 0 / Unlimited 1).
namespace OverflowBehaviorToken {
inline constexpr QLatin1StringView Float{"float"}; ///< AutotileOverflowBehavior::Float (0)
inline constexpr QLatin1StringView Unlimited{"unlimited"}; ///< Unlimited (1)
} // namespace OverflowBehaviorToken

/// Wire tokens for SetDragBehavior's `value` param. Ints match
/// PhosphorTiles::AutotileDragBehavior (Float 0 / Reorder 1).
namespace DragBehaviorToken {
inline constexpr QLatin1StringView Float{"float"}; ///< AutotileDragBehavior::Float (0)
inline constexpr QLatin1StringView Reorder{"reorder"}; ///< Reorder (1)
} // namespace DragBehaviorToken

/// Wire tokens for SetCenterFocusedColumn's `value` param — the closed vocabulary
/// the descriptor validator, the daemon consumer
/// (LayoutRegistry::resolveContextScrollingParams maps token → int), and the
/// settings label layers all read from this single source.
namespace CenterFocusedColumnToken {
inline constexpr QLatin1StringView Never{"never"}; ///< never re-centre (0)
inline constexpr QLatin1StringView Always{"always"}; ///< always re-centre (1)
inline constexpr QLatin1StringView OnOverflow{"onOverflow"}; ///< re-centre only when the row overflows (2)
} // namespace CenterFocusedColumnToken

/// Wire tokens for SetScrollDefaultColumnDisplay's `value` param — how a column
/// lays its windows out. Ints match the scrolling engine's column display mode.
namespace ColumnDisplayToken {
inline constexpr QLatin1StringView Normal{"normal"}; ///< windows share the column vertically (0)
inline constexpr QLatin1StringView Tabbed{"tabbed"}; ///< windows stack as tabs (1)
} // namespace ColumnDisplayToken

/// Wire tokens for SetTabIndicatorStyle's `value` param. Ints match
/// ConfigDefaults' TabIndicatorStyle vocabulary.
namespace TabIndicatorStyleToken {
inline constexpr QLatin1StringView Chips{"chips"}; ///< a pill of titled chips (0)
inline constexpr QLatin1StringView Bar{"bar"}; ///< a run of coloured segments (1)
} // namespace TabIndicatorStyleToken

/// Wire tokens for SetTabIndicatorPosition's `value` param — which column edge
/// the indicator runs along. Ints match the engine's TabIndicatorPosition.
namespace TabIndicatorPositionToken {
inline constexpr QLatin1StringView Left{"left"}; ///< (0)
inline constexpr QLatin1StringView Right{"right"}; ///< (1)
inline constexpr QLatin1StringView Top{"top"}; ///< (2)
inline constexpr QLatin1StringView Bottom{"bottom"}; ///< (3)
} // namespace TabIndicatorPositionToken

/// Wire tokens for OpenColumnPlacement's `value` param — whether an opening
/// window starts its own column or joins the focused one.
namespace ColumnPlacementToken {
inline constexpr QLatin1StringView NewColumn{"newColumn"}; ///< open in a column of its own
inline constexpr QLatin1StringView Consume{"consume"}; ///< consume into the focused column
} // namespace ColumnPlacementToken

/// Wire tokens for SetScrollInsertPosition's `value` param — where a fresh
/// column enters the strip. Ints match the scrolling engine's
/// ScrollInsertPosition (rightOfActive 0 … intoActiveColumn 4); the schema
/// side pins the same spellings (settingsschema_scrolling.cpp intChoices).
namespace ScrollInsertPositionToken {
inline constexpr QLatin1StringView RightOfActive{"rightOfActive"};
inline constexpr QLatin1StringView LeftOfActive{"leftOfActive"};
inline constexpr QLatin1StringView First{"first"};
inline constexpr QLatin1StringView Last{"last"};
inline constexpr QLatin1StringView IntoActiveColumn{"intoActiveColumn"};
} // namespace ScrollInsertPositionToken

/// Wire tokens for SetScrollStickyWindowHandling's `value` param — how the
/// strip treats a window shown on all desktops. Ints match
/// `PhosphorEngine::StickyWindowHandling` (treatAsNormal 0 / restoreOnly 1 /
/// ignoreAll 2) and the spellings match the settings schema's intChoices
/// (settingsschema_scrolling.cpp) so a rule and the settings page name the
/// same thing. The SCROLLING engine collapses both non-normal values to
/// "float the window" at its single consumption site; the distinction is
/// preserved on the wire because the snapping and tiling engines honour it
/// and a future scrolling consumer may too.
namespace StickyWindowHandlingToken {
inline constexpr QLatin1StringView TreatAsNormal{"treatAsNormal"};
inline constexpr QLatin1StringView RestoreOnly{"restoreOnly"};
inline constexpr QLatin1StringView IgnoreAll{"ignoreAll"};
} // namespace StickyWindowHandlingToken

/// Wire tokens for SetScrollStripAxis's `value` param — which way the matched
/// context's strip runs. Ints match the Scrolling.StripAxis config space
/// (auto 0 / horizontal 1 / vertical 2) and the spellings match the settings
/// schema's intChoices (settingsschema_scrolling.cpp) so a rule and the
/// setting it overrides name the same thing. This is the INTENT space, not
/// PhosphorProtocol::ScrollAxis: auto has no engine enumerator (it resolves
/// from the work-area shape at relayout), and the two spaces number
/// horizontal differently, so never cast between them.
namespace StripAxisToken {
inline constexpr QLatin1StringView Auto{"auto"};
inline constexpr QLatin1StringView Horizontal{"horizontal"};
inline constexpr QLatin1StringView Vertical{"vertical"};
} // namespace StripAxisToken

/// Wire tokens for SetWindowLayer's `value` param — the closed vocabulary the
/// descriptor validator, the KWin-effect consumer (resolveWindowLayer), and the
/// settings label layers all read from this single source. The tokens map onto
/// KWin's keepAbove/keepBelow pair: above = keepAbove, below = keepBelow,
/// normal = both off.
namespace WindowLayerToken {
inline constexpr QLatin1StringView Above{"above"};
inline constexpr QLatin1StringView Normal{"normal"};
inline constexpr QLatin1StringView Below{"below"};
} // namespace WindowLayerToken

/// Sentinel value a `SetBorderColorActive` / `SetBorderColorInactive` `value`
/// param may carry instead of a hex string, meaning "track the live system
/// accent colour". The
/// descriptor validator admits it alongside the hex shapes; the consumer (the
/// KWin effect's border resolver) substitutes the current accent at apply time
/// so the colour follows a Plasma accent change without a rule edit.
namespace BorderColorToken {
inline constexpr QLatin1StringView Accent{"accent"};
} // namespace BorderColorToken

} // namespace PhosphorRules

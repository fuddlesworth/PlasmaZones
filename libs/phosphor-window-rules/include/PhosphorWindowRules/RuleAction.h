// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1StringView>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "phosphorwindowrules_export.h"

namespace PhosphorWindowRules {

/**
 * @brief Which evaluation pass an action contributes to.
 *
 * The evaluator runs two distinct query shapes against the rule set:
 *  - **Context queries** (windowless) — issued by the screen/desktop/activity
 *    resolution paths to ask "what engine mode / layout does this context use?".
 *    They carry only `ScreenId` / `VirtualDesktop` / `Activity`; every
 *    window-property predicate evaluates false against them.
 *  - **Window queries** — issued per window to ask "what per-window behaviour
 *    applies?". They carry both window properties and the window's context.
 *
 * An action's domain selects which query shape can fill its slot. Authoring a
 * @c Context action with a match that pins window properties produces a rule
 * that silently never fires (the window predicate fails during context
 * resolution); the validator (`WindowRule::validationIssues()`) flags exactly
 * this case so the picker UI and the JSON loader can surface it.
 */
enum class ActionDomain : int {
    Context = 0, ///< fills slots consumed by context (screen/desktop/activity) resolution
    Window = 1, ///< fills slots consumed by per-window evaluation
};

/**
 * @brief One action carried by a WindowRule.
 *
 * A RuleAction is a `{ type, params }` pair. The `type` selects an
 * ActionDescriptor in the ActionRegistry; the descriptor maps the action to
 * a **slot** and validates/serializes its `params`.
 *
 * Conflict resolution during evaluation is **first-matching-rule-wins per
 * slot** — actions in different slots stack, actions in the same slot do not.
 * Some action types compute their slot from a `params` field (the animation
 * actions are scoped by their `event`), so the slot is resolved through the
 * registry rather than stored on the action.
 */
struct PHOSPHORWINDOWRULES_EXPORT RuleAction
{
    QString type; ///< registered action type id (e.g. `ActionType::SetEngineMode`)
    /// Action-specific payload. NOTE: `"type"` is a **reserved param key** —
    /// `toJson()` writes `type` inline alongside `params`, so a `params`
    /// entry keyed `"type"` is clobbered on serialization (and stripped on
    /// load). A free-form `acceptAny` action must not use `"type"` as a
    /// payload key.
    QJsonObject params; ///< action-specific payload

    bool operator==(const RuleAction& other) const
    {
        return type == other.type && params == other.params;
    }
    bool operator!=(const RuleAction& other) const
    {
        return !(*this == other);
    }

    QJsonObject toJson() const;

    /// Strict loader — validates against the ActionRegistry; returns nullopt
    /// for an unregistered type or params that fail descriptor validation.
    static std::optional<RuleAction> fromJson(const QJsonObject& obj);
};

/**
 * @brief Structural schema for one action param.
 *
 * The schema carries enough information for the editor UI to render an
 * input widget without the UI layer hand-maintaining a parallel per-type
 * switch. `kind` is a UI-side hint string (the canonical kinds are
 * `string`, `number`, `percent`, `enum`, `bool`, `color`, plus the
 * picker-aware kinds `snappingLayout`, `tilingAlgorithm`, `animationEvent`,
 * `shaderEffect`, `overlayShader`, `zoneOrdinals`, `curveEditor`); QML loaders dispatch on it. Labels stay in
 * the GPL settings layer because they need translation through PhosphorI18n::tr —
 * the lib only owns the structural part of the schema.
 *
 * The optional fields are populated by kind:
 *   - `number` / `percent` may carry `min` / `max` (display-unit bounds)
 *     and `scale` (stored = display * scale; e.g. percent uses 0.01).
 *   - `enum` carries `enumWireValues` — the wire strings the picker
 *     offers; labels for each are translated upstream.
 *   - `bool` is a toggle (wire value is a JSON bool); may carry
 *     `defaultDisplay` (1.0 → seed true, 0.0 → seed false).
 *   - `color` is a `#AARRGGBB` hex string (alpha-first, matching
 *     QColor::HexArgb); seeded by the settings layer's `defaultPayloadFor`
 *     "color" branch (defaultDisplay is a double, so it cannot carry a colour
 *     seed). Shorter `#RGB`/`#RRGGBB` shapes still load for hand-edited values.
 *   - picker-aware kinds carry no schema state — the QML loader knows
 *     to swap in the catalogue-driven ComboBox.
 */
struct PHOSPHORWINDOWRULES_EXPORT ParamSchema
{
    QString key{}; ///< wire param key in `RuleAction::params`
    QString kind{}; ///< UI kind hint — see struct doc
    std::optional<double> min{}; ///< inclusive lower bound, in display units
    std::optional<double> max{}; ///< inclusive upper bound, in display units
    std::optional<double> scale{}; ///< stored = display * scale; nullopt for unscaled kinds
    /// Initial display-unit value for the editor when a fresh rule is created.
    /// Falls back to `min` when unset. Needed for kinds whose `min` is a valid
    /// but undesirable starting value — e.g. SetOpacity's `min = 0` means a
    /// fresh rule would seed at 0% (invisible window) and saveable as-is; the
    /// descriptor instead declares `defaultDisplay = 100.0` so the user starts
    /// at "no visible change" and must deliberately lower it.
    std::optional<double> defaultDisplay{};
    QStringList enumWireValues{}; ///< wire values for `kind == "enum"`; empty otherwise
};

// ── Action tags — structural metadata for consumer-side filtering ──────────
// Tags classify actions into overlapping behavioural groups. Consumers use
// `ActionRegistry::hasTag()` / `typesWithTag()` instead of maintaining
// parallel type-list helpers. Adding a tag to a new action is one descriptor
// field edit; no consumer call-site changes. Tags compose via intersection:
// e.g. the set that `isAnimationOverrideAction` covers (the 3 Override*
// actions, NOT ExcludeAnimations) = Tag::Animation ∩ Tag::Effect.
namespace Tag {
/// Actions consumed by the KWin effect's rule set (shader manager +
/// border appearance). ExcludeAnimations deliberately omits this tag.
inline constexpr QLatin1StringView Effect{"effect"};
inline constexpr QLatin1StringView Border{"border"};
inline constexpr QLatin1StringView Animation{"animation"};
inline constexpr QLatin1StringView LayoutEngine{"layoutEngine"};
inline constexpr QLatin1StringView Gap{"gap"};
/// Context-domain overlay-property overrides (overlay shader / style).
/// Consumed daemon-side by the overlay service via
/// `LayoutRegistry::resolveContextOverlay` — NOT by the KWin effect, so these
/// actions deliberately omit `Tag::Effect`.
inline constexpr QLatin1StringView Overlay{"overlay"};
} // namespace Tag

/**
 * @brief Static metadata describing one registered action type.
 *
 * Adding a future rule type is registering one of these — no new matcher, no
 * new file, no new UI page. The descriptor owns:
 *   - the `slotFor` resolver (a function so animation actions can scope their
 *     slot by an `event` param),
 *   - a `validate` predicate run on load,
 *   - the set of param keys the action accepts (`allowedKeys`) — the strict
 *     loader rejects any action carrying a key not in this set,
 *   - whether the action is **terminal** (an `Exclude` action stops evaluation),
 *   - the structural `params` schema consumed by the editor UI,
 *   - the `userAuthorable` visibility flag used by the action-type picker
 *     to filter out actions that are registered for back-compat / loader
 *     completeness but should not appear in the new-rule wizard, and
 *   - `category` / `displayOrder` / `tags` for UI grouping and consumer-side
 *     filtering.
 */
struct PHOSPHORWINDOWRULES_EXPORT ActionDescriptor
{
    /// Function shape that resolves the slot for a concrete action's params.
    /// Hoisted to a type alias so the registry initialiser body stays
    /// readable (the std::function template is verbose enough that inlining
    /// it at every descriptor pushes the literal off-screen).
    using SlotResolver = std::function<QString(const QJsonObject& params)>;
    using Validator = std::function<bool(const QJsonObject& params)>;

    QString type; ///< the action type id
    /// Resolves the slot for a concrete action's params. Returning an empty
    /// string means the action contributes no slot (treated as invalid).
    SlotResolver slotFor;
    /// Returns true if @p params is a well-formed payload for this type.
    Validator validate;
    /// Terminal actions (Exclude) stop evaluation once their rule matches.
    bool terminal = false;
    /// The complete set of param keys this action type accepts. The strict
    /// loader (`RuleAction::fromJson`) rejects an action whose `params`
    /// carries any key outside this set. An **empty** set disables the
    /// strict-key check — used for free-form / future-extensible action
    /// types whose params payload is deliberately open.
    QStringList allowedKeys{};
    /// Which evaluation pass the action contributes to — see @ref ActionDomain.
    /// The validator combines this with the rule's match expression to detect
    /// rules that silently never fire (a context action against a match that
    /// pins window properties).
    ActionDomain domain = ActionDomain::Window;
    /// Structural per-param schema consumed by the editor UI. Order is
    /// the order the editor renders the param widgets in. An action with
    /// no params (e.g. `Float`, `Exclude`) leaves this empty.
    QList<ParamSchema> params{};
    /// True when the action should appear in the editor's "add rule"
    /// type picker. Set to false on actions that are registered for
    /// loader / back-compat completeness but whose semantics aren't
    /// authorable through the standard wizard (e.g. an action whose
    /// runtime consumer hasn't shipped yet).
    bool userAuthorable = true;
    /// UI grouping category for the action-type picker. Actions with the
    /// same category are clustered together; empty means uncategorized.
    QString category{};
    /// Sort key within a category (lower = earlier). Actions with the same
    /// displayOrder are sorted by type string.
    int displayOrder = 0;
    /// Behavioural tags for consumer-side filtering. Values are from the
    /// `Tag::` namespace constants. Use `ActionRegistry::hasTag()` /
    /// `typesWithTag()` for queries.
    QStringList tags{};
};

/**
 * @brief Process-wide registry of action descriptors.
 *
 * The built-in descriptors register on first access via `instance()`. The
 * registry is the single source of truth for an action's slot, validation,
 * and terminal flag — `RuleAction`, `RuleEvaluator`, and the loaders all
 * consult it.
 */
class PHOSPHORWINDOWRULES_EXPORT ActionRegistry
{
public:
    /// The shared process-wide registry, pre-populated with the built-ins.
    static ActionRegistry& instance();

    /// Register (or replace) a descriptor. Used by the built-ins and by
    /// future-phase consumers that add new action types.
    void registerAction(const ActionDescriptor& descriptor);

    /// Remove a previously-registered descriptor. Returns true if @p type
    /// was present and removed, false otherwise. Counterpart to
    /// @ref registerAction so tests / future consumers can symmetrically
    /// undo a registration (without it, every test-time sentinel would
    /// leak into the process-wide singleton for the remainder of the
    /// test binary's lifetime).
    bool unregisterAction(const QString& type);

    /// True if @p type names a registered action.
    bool isRegistered(const QString& type) const;

    /// The descriptor for @p type, or nullopt if unregistered.
    std::optional<ActionDescriptor> descriptor(const QString& type) const;

    /// The slot a concrete @p action resolves to, or an empty string if the
    /// type is unregistered or its descriptor rejects the params.
    QString slotFor(const RuleAction& action) const;

    /// True if @p action is a terminal action (Exclude).
    bool isTerminal(const RuleAction& action) const;

    /// True if @p action passes its descriptor's validation.
    bool validate(const RuleAction& action) const;

    /// The evaluation domain @p action contributes to, or @c ActionDomain::Window
    /// for an unregistered type (the conservative default — window evaluation
    /// is the broader query shape, so an unknown action gets the looser
    /// compatibility check rather than being silently flagged as a context
    /// mismatch).
    ActionDomain domainFor(const RuleAction& action) const;

    /// All registered type ids — for UI enumeration / tests.
    QStringList registeredTypes() const;

    /// True if the descriptor for @p type carries @p tag.
    bool hasTag(const QString& type, QLatin1StringView tag) const;

    /// All registered type ids whose descriptor carries @p tag.
    QStringList typesWithTag(QLatin1StringView tag) const;

private:
    ActionRegistry();
    void registerBuiltins();

    QHash<QString, ActionDescriptor> m_descriptors;
};

// ── Built-in action type ids — canonical wire strings ──
namespace ActionType {
inline constexpr QLatin1StringView SetEngineMode{"setEngineMode"};
inline constexpr QLatin1StringView SetSnappingLayout{"setSnappingLayout"};
inline constexpr QLatin1StringView SetTilingAlgorithm{"setTilingAlgorithm"};
inline constexpr QLatin1StringView DisableEngine{"disableEngine"};
/// Lock the active layout for the matched screen/desktop/activity context so
/// it can't be switched — the rule-driven equivalent of the manual
/// ToggleLayoutLock shortcut. Context domain (matches only context fields);
/// mode-agnostic (a `true` lock applies to both the snapping and tiling
/// engines). The daemon resolves it LIVE on the context-lock path and never
/// persists it, so rule locks and manual toggles never overwrite each other.
inline constexpr QLatin1StringView LockContext{"lockContext"};
inline constexpr QLatin1StringView Exclude{"exclude"};
inline constexpr QLatin1StringView Float{"float"};
/// Snap a matched window into one or more zones on open. Carries a non-empty
/// list of 1-based zone ordinals (`ActionParam::Zones`); a single ordinal snaps
/// to that zone, multiple ordinals snap to their unioned bounding rect (zone
/// spanning). Ordinals are layout-agnostic — they address "zone N of whatever
/// layout is active on the window's screen", matching the snapToZone1..9
/// shortcuts. Daemon-consumed (placement) on the SnapEngine open path; supersedes
/// the retired per-layout `Layout::appRules`. Domain Window.
inline constexpr QLatin1StringView SnapToZone{"snapToZone"};
inline constexpr QLatin1StringView OverrideAnimationShader{"overrideAnimationShader"};
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
/// Disable every animation override on a matched window. The opposite of
/// the OverrideAnimation* family — the effect's shouldAnimateWindow gate
/// surfaces this as "no animation for this window, regardless of other
/// rules". Distinct from the generic `Exclude` action (which marks the
/// window unmanaged by snap/tile/etc); a user can have `Exclude` without
/// `ExcludeAnimations` and vice versa. Migrated from the legacy
/// animationExcludedApplications / animationExcludedWindowClasses
/// settings lists by the v3→v4 chain.
inline constexpr QLatin1StringView ExcludeAnimations{"excludeAnimations"};

/// Per-window override for floated-position restore on login. A boolean `value`
/// action: true forces the window's previous floated position (and original
/// monitor) to be restored, false suppresses it. Engine-neutral — overrides the
/// per-engine `snappingRestoreFloatedWindowsOnLogin` /
/// `autotileRestoreFloatedWindowsOnLogin` settings for matched windows. Resolved
/// by the daemon-injected restore-position predicate and consulted inside both
/// SnapEngine::resolveWindowRestore and AutotileEngine::insertWindow. Domain
/// Window (matches window properties).
inline constexpr QLatin1StringView RestorePosition{"restorePosition"};

// ── Per-window border / title-bar appearance overrides (domain Window) ──
// Effect-side per-window overrides of the global snap appearance. Each is its
// own slot so independent rules cascade per-property (a width rule and a
// colour rule on separate rules both apply). Applied to ANY matched window
// (snapped or floating), mirroring SetOpacity.
inline constexpr QLatin1StringView SetHideTitleBar{"setHideTitleBar"};
inline constexpr QLatin1StringView SetBorderVisible{"setBorderVisible"};
inline constexpr QLatin1StringView SetBorderWidth{"setBorderWidth"};
inline constexpr QLatin1StringView SetBorderRadius{"setBorderRadius"};
// Focus-dependent colours are expressed by matching on `Field::IsFocused`
// (e.g. a rule `WHEN NOT focused ⇒ SetBorderColor(gray)`) rather than a
// separate inactive-colour action — see the effect's updateWindowBorder.
inline constexpr QLatin1StringView SetBorderColor{"setBorderColor"};

// ── Per-context gap overrides (domain Context) ──
// Resolved daemon-side at zone-geometry time as the highest-precedence gap
// layer (rule > per-screen > layout > global). Match on screen / desktop /
// activity; per-property to mirror the PerScreenSnappingKey set.
inline constexpr QLatin1StringView SetZonePadding{"setZonePadding"};
inline constexpr QLatin1StringView SetOuterGap{"setOuterGap"};
inline constexpr QLatin1StringView SetUsePerSideOuterGap{"setUsePerSideOuterGap"};
inline constexpr QLatin1StringView SetOuterGapTop{"setOuterGapTop"};
inline constexpr QLatin1StringView SetOuterGapBottom{"setOuterGapBottom"};
inline constexpr QLatin1StringView SetOuterGapLeft{"setOuterGapLeft"};
inline constexpr QLatin1StringView SetOuterGapRight{"setOuterGapRight"};

/// @deprecated Use `ActionRegistry::hasTag(type, Tag::Animation) && hasTag(type, Tag::Effect)` instead.
[[deprecated("use ActionRegistry::hasTag with Tag::Animation ∩ Tag::Effect")]]
inline bool isAnimationOverrideAction(const QString& type)
{
    return type == OverrideAnimationShader || type == OverrideAnimationTiming || type == OverrideAnimationCurve;
}

/// @deprecated Use `ActionRegistry::hasTag(type, Tag::Border)` instead.
[[deprecated("use ActionRegistry::hasTag with Tag::Border")]]
inline bool isBorderAppearanceAction(const QString& type)
{
    return type == SetHideTitleBar || type == SetBorderVisible || type == SetBorderWidth || type == SetBorderRadius
        || type == SetBorderColor;
}

/// @deprecated Use `ActionRegistry::hasTag(type, Tag::Effect)` instead.
/// Note: ExcludeAnimations deliberately omits Tag::Effect — it flows into the
/// effect's separate `m_animationExclusionRuleSet`.
[[deprecated("use ActionRegistry::hasTag with Tag::Effect")]]
inline bool isEffectRuleAction(const QString& type)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return isAnimationOverrideAction(type) || type == SetOpacity || isBorderAppearanceAction(type);
#pragma GCC diagnostic pop
}

/// @deprecated Use `ActionRegistry::hasTag(type, Tag::LayoutEngine)` instead.
[[deprecated("use ActionRegistry::hasTag with Tag::LayoutEngine")]]
inline bool isLayoutEngineContextAction(const QString& type)
{
    return type == SetEngineMode || type == SetSnappingLayout || type == SetTilingAlgorithm || type == DisableEngine
        || type == LockContext;
}
} // namespace ActionType

// ── Action param keys — canonical wire strings ──
//
// Param-key vocabulary shared across every wire-shape reader (the registry
// validators in ruleaction.cpp, the config-layer v3→v4 migration that ports
// legacy AnimationAppRule entries, the rule-editor UI, and the KWin-effect-
// side resolvers in `kwin-effect/plasmazoneseffect/shader_resolve.cpp`).
// A future rename (e.g. `effectId` → `effect_id`) updates one entry here and
// flows everywhere instead of being hard-coded at four call sites.
namespace ActionParam {
// OverrideAnimation{Shader,Timing,Curve} family.
inline constexpr QLatin1StringView Event{"event"};
inline constexpr QLatin1StringView EffectId{"effectId"};
inline constexpr QLatin1StringView Params{"params"};
inline constexpr QLatin1StringView Curve{"curve"};
inline constexpr QLatin1StringView DurationMs{"durationMs"};
// SetOpacity payload — the wire-encoded opacity is a [0.0, 1.0] double.
inline constexpr QLatin1StringView Value{"value"};
// SetEngineMode / DisableEngine engine-token key — the wire token vocabulary
// is `PhosphorZones::modeToWireString(Mode)` (snapping / autotile / scrolling).
inline constexpr QLatin1StringView Mode{"mode"};
// SetSnappingLayout layout-id key — wire is a `{uuid-with-braces}` string.
inline constexpr QLatin1StringView LayoutId{"layoutId"};
// SetTilingAlgorithm algorithm-token key — wire is the algorithm registry id.
inline constexpr QLatin1StringView Algorithm{"algorithm"};
// SnapToZone target-zone key — wire is a non-empty JSON array of 1-based zone
// ordinals (e.g. `[1]` or `[1, 2]`); multiple entries snap to their union.
inline constexpr QLatin1StringView Zones{"zones"};
} // namespace ActionParam

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

// ── Built-in slot ids ──
namespace ActionSlot {
inline constexpr QLatin1StringView EngineMode{"engine-mode"};
inline constexpr QLatin1StringView Layout{"layout"};
inline constexpr QLatin1StringView EngineEnable{"engine-enable"};
/// Context-domain layout-lock slot — filled by `ActionType::LockContext`.
/// A single boolean: a winning rule with `value == true` locks the context.
inline constexpr QLatin1StringView Locked{"locked"};
inline constexpr QLatin1StringView Manage{"manage"};
inline constexpr QLatin1StringView Float{"float"};
/// Window-scoped open-placement slot — filled by `ActionType::SnapToZone`. A
/// single slot (first-matching-rule-wins), carrying the zone-ordinal list
/// (`ActionParam::Zones`) the daemon snaps the opening window into. Mutually
/// resolved against `Float` on the open path (a float rule opts out of snapping).
inline constexpr QLatin1StringView Placement{"placement"};
inline constexpr QLatin1StringView Opacity{"opacity"};
inline constexpr QLatin1StringView RestorePosition{"restore-position"};
// Per-window border / title-bar appearance slots (one per property so
// independent rules cascade per-property).
inline constexpr QLatin1StringView HideTitleBar{"hide-title-bar"};
inline constexpr QLatin1StringView BorderVisible{"border-visible"};
inline constexpr QLatin1StringView BorderWidth{"border-width"};
inline constexpr QLatin1StringView BorderRadius{"border-radius"};
inline constexpr QLatin1StringView BorderColor{"border-color"};
// Per-context gap slots (mirror the PerScreenSnappingKey set).
inline constexpr QLatin1StringView ZonePadding{"zone-padding"};
inline constexpr QLatin1StringView OuterGap{"outer-gap"};
inline constexpr QLatin1StringView UsePerSideOuterGap{"use-per-side-outer-gap"};
inline constexpr QLatin1StringView OuterGapTop{"outer-gap-top"};
inline constexpr QLatin1StringView OuterGapBottom{"outer-gap-bottom"};
inline constexpr QLatin1StringView OuterGapLeft{"outer-gap-left"};
inline constexpr QLatin1StringView OuterGapRight{"outer-gap-right"};
// Per-context overlay-property slots (one per property so independent rules
// cascade per-property). Filled by the OverrideOverlay* context actions, read
// by `LayoutRegistry::resolveContextOverlay`. OverlayShader carries the shader
// effect id (ActionParam::EffectId); OverlayStyle carries a wire token
// (ActionParam::Value).
inline constexpr QLatin1StringView OverlayShader{"overlay-shader"};
inline constexpr QLatin1StringView OverlayStyle{"overlay-style"};
// Animation slots are event-scoped: "anim-shader:<event>" / "anim-timing:<event>"
// / "anim-curve:<event>". Curve and timing are split so they can be overridden
// independently per event — `resolveAnimationMotionProfile` reads the curve
// slot first and falls back to the timing slot's curve field for legacy rules.
inline constexpr QLatin1StringView AnimShaderPrefix{"anim-shader:"};
inline constexpr QLatin1StringView AnimTimingPrefix{"anim-timing:"};
inline constexpr QLatin1StringView AnimCurvePrefix{"anim-curve:"};
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
} // namespace ActionSlot

} // namespace PhosphorWindowRules

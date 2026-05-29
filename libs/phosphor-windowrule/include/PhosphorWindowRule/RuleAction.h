// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "phosphorwindowrule_export.h"

namespace PhosphorWindowRule {

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
struct PHOSPHORWINDOWRULE_EXPORT RuleAction
{
    QString type; ///< registered action type id (e.g. "setEngineMode")
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
 * `string`, `number`, `percent`, `enum`, plus the picker-aware kinds
 * `snappingLayout`, `tilingAlgorithm`, `animationEvent`, `shaderEffect`,
 * `curveEditor`); QML loaders dispatch on it. Labels stay in the GPL
 * settings layer because they need translation through PzI18n::tr —
 * the lib only owns the structural part of the schema.
 *
 * The optional fields are populated by kind:
 *   - `number` / `percent` may carry `min` / `max` (display-unit bounds)
 *     and `scale` (stored = display * scale; e.g. percent uses 0.01).
 *   - `enum` carries `enumWireValues` — the wire strings the picker
 *     offers; labels for each are translated upstream.
 *   - picker-aware kinds carry no schema state — the QML loader knows
 *     to swap in the catalogue-driven ComboBox.
 */
struct PHOSPHORWINDOWRULE_EXPORT ParamSchema
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
 *   - the structural `params` schema consumed by the editor UI, and
 *   - the `userAuthorable` visibility flag used by the action-type picker
 *     to filter out actions that are registered for back-compat / loader
 *     completeness but should not appear in the new-rule wizard.
 */
struct PHOSPHORWINDOWRULE_EXPORT ActionDescriptor
{
    /// Function shape that resolves the slot for a concrete action's params.
    /// Hoisted to a type alias so the registry initialiser body stays
    /// readable (the std::function template is verbose enough that inlining
    /// it twelve times pushes the descriptor literal off-screen).
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
};

/**
 * @brief Process-wide registry of action descriptors.
 *
 * The built-in descriptors register on first access via `instance()`. The
 * registry is the single source of truth for an action's slot, validation,
 * and terminal flag — `RuleAction`, `RuleEvaluator`, and the loaders all
 * consult it.
 */
class PHOSPHORWINDOWRULE_EXPORT ActionRegistry
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
inline constexpr QLatin1StringView Exclude{"exclude"};
inline constexpr QLatin1StringView Float{"float"};
inline constexpr QLatin1StringView OverrideAnimationShader{"overrideAnimationShader"};
inline constexpr QLatin1StringView OverrideAnimationTiming{"overrideAnimationTiming"};
/// Curve-only animation override — separate slot from timing so a user can
/// override the easing/spring curve for an event without committing to a
/// duration (and vice versa). `OverrideAnimationTiming` still carries both
/// for backward compatibility with legacy rules; `resolveAnimationMotionProfile`
/// checks the curve slot first.
inline constexpr QLatin1StringView OverrideAnimationCurve{"overrideAnimationCurve"};
inline constexpr QLatin1StringView SetOpacity{"setOpacity"};

/// True when @p type is one of the three OverrideAnimation* action wire
/// strings — shader / timing / curve. The trio shares the same cascade
/// (animation event resolution + per-window scope) so call-sites repeatedly
/// need the same three-way OR; this helper keeps the action-type list in one
/// place so adding a fourth override variant only updates here.
inline bool isAnimationOverrideAction(const QString& type)
{
    return type == OverrideAnimationShader || type == OverrideAnimationTiming || type == OverrideAnimationCurve;
}

/// True when @p type is one of the actions the KWin effect's window-rule
/// evaluator consumes — the three OverrideAnimation* variants plus SetOpacity.
/// The shader-transition manager loads rules carrying any of these so the
/// effect side can resolve them per paint; rules without one of these actions
/// never need to reach the effect-side evaluator. Keeping the predicate
/// alongside `isAnimationOverrideAction` ensures adding a new effect-consumed
/// action type updates the filter list in one place.
inline bool isEffectRuleAction(const QString& type)
{
    return isAnimationOverrideAction(type) || type == SetOpacity;
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
} // namespace ActionParam

// ── Built-in slot ids ──
namespace ActionSlot {
inline constexpr QLatin1StringView EngineMode{"engine-mode"};
inline constexpr QLatin1StringView Layout{"layout"};
inline constexpr QLatin1StringView EngineEnable{"engine-enable"};
inline constexpr QLatin1StringView Manage{"manage"};
inline constexpr QLatin1StringView Float{"float"};
inline constexpr QLatin1StringView Opacity{"opacity"};
// Animation slots are event-scoped: "anim-shader:<event>" / "anim-timing:<event>"
// / "anim-curve:<event>". Curve and timing are split so they can be overridden
// independently per event — `resolveAnimationMotionProfile` reads the curve
// slot first and falls back to the timing slot's curve field for legacy rules.
inline constexpr QLatin1StringView AnimShaderPrefix{"anim-shader:"};
inline constexpr QLatin1StringView AnimTimingPrefix{"anim-timing:"};
inline constexpr QLatin1StringView AnimCurvePrefix{"anim-curve:"};
} // namespace ActionSlot

} // namespace PhosphorWindowRule

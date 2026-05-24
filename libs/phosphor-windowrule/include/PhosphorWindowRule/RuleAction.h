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
 * @brief Static metadata describing one registered action type.
 *
 * Adding a future rule type is registering one of these — no new matcher, no
 * new file, no new UI page. The descriptor owns:
 *   - the `slotFor` resolver (a function so animation actions can scope their
 *     slot by an `event` param),
 *   - a `validate` predicate run on load,
 *   - the set of param keys the action accepts (`allowedKeys`) — the strict
 *     loader rejects any action carrying a key not in this set,
 *   - whether the action is **terminal** (an `Exclude` action stops evaluation).
 */
struct PHOSPHORWINDOWRULE_EXPORT ActionDescriptor
{
    QString type; ///< the action type id
    /// Resolves the slot for a concrete action's params. Returning an empty
    /// string means the action contributes no slot (treated as invalid).
    std::function<QString(const QJsonObject& params)> slotFor;
    /// Returns true if @p params is a well-formed payload for this type.
    std::function<bool(const QJsonObject& params)> validate;
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
} // namespace ActionType

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

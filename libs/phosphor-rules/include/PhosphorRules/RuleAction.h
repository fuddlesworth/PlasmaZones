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

#include "phosphorrules_export.h"

// The action VOCABULARY — type ids, param keys with their shared validation
// bounds and token sets, and slot ids — lives in three companion headers,
// split off purely for file size. They are included here rather than left to
// the consumer: this header is the one every consumer already includes, and
// the machinery below is unusable without the vocabulary, so the split must
// not become an include-ordering puzzle at 40-odd call sites.
#include "ActionParams.h"
#include "ActionSlots.h"
#include "ActionTypes.h"

namespace PhosphorRules {

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
 * resolution); the validator (`Rule::validationIssues()`) flags exactly
 * this case so the picker UI and the JSON loader can surface it.
 */
enum class ActionDomain : int {
    Context = 0, ///< fills slots consumed by context (screen/desktop/activity) resolution
    Window = 1, ///< fills slots consumed by per-window evaluation
};

/**
 * @brief One action carried by a Rule.
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
struct PHOSPHORRULES_EXPORT RuleAction
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
 * picker-aware kinds `snappingLayout`, `tilingAlgorithm`,
 * `scrollingTemplate`, `animationEvent`, `shaderEffect`, `overlayShader`,
 * `zoneOrdinals`, `zoneNames`, `curveEditor`, `screenId`,
 * `virtualDesktop`, `decorationChain`); QML loaders dispatch on it. Labels stay in the GPL
 * settings layer because they need translation through PhosphorI18n::tr —
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
 *   - picker-aware kinds normally carry no schema state — the QML loader
 *     knows to swap in the catalogue-driven ComboBox — though one MAY carry
 *     advisory bounds (RouteToDesktop's `virtualDesktop` declares min/max);
 *     the loader dispatches on `kind` alone either way.
 */
struct PHOSPHORRULES_EXPORT ParamSchema
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
// e.g. the animation-override actions (the 3 Override* actions, NOT
// ExcludeAnimations) are exactly Tag::Animation ∩ Tag::Effect.
//
// RETAINED PUBLIC API: Tag::Border, Tag::Gap, Tag::LayoutEngine and
// Tag::Overlay currently have no in-tree reader, and `typesWithTag()` has no
// in-tree call site (tests included) — every consumer that could use them
// happens to filter by slot instead. They stay for the same reason
// PhosphorZones::LayoutAssignmentKey does (AssignmentEntry.h): this is an
// installed LGPL library header whose point is third-party linkage, the tag
// vocabulary is the documented classification of the action set, and removing
// an exported constant or query is a source break for consumers we do not
// control. Deliberately kept, not overlooked.
namespace Tag {
/// Actions consumed by the KWin effect's rule set (shader manager +
/// border appearance + window stacking layer). ExcludeAnimations
/// deliberately omits this tag.
///
/// This tag admits a rule into the effect's APPEARANCE rule set, whose
/// evaluator honours `ExcludeAnimations` in its terminal-action scope and
/// whose any-match gate force-animates the matched window. An effect-consumed
/// action that is neither an appearance override nor something a user's "no
/// animations" rule should cancel therefore belongs on @ref EffectVerdict, not
/// here.
inline constexpr QLatin1StringView Effect{"effect"};
/// Effect-consumed one-shot verdicts that are NOT appearance overrides and
/// must not be cancelled by ExcludeAnimations.
///
/// The actions carrying it (OpenFullscreen, ScrollFactor) are delivered to the
/// compositor exactly like a @ref Effect action, but through a separate
/// evaluator whose terminal-action scope admits only the blanket `Exclude`.
/// Riding @ref Effect would put them behind the animation evaluator, where any
/// matching `ExcludeAnimations` rule breaks the walk before their slots fill —
/// so "no animations for this window" would silently cancel the scroll
/// multiplier and the open-fullscreen verdict — and would additionally feed
/// the animation set's "any match force-animates" opt-in gate with a rule that
/// says nothing about animation.
inline constexpr QLatin1StringView EffectVerdict{"effectVerdict"};
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
 *   - whether the action is **terminal** (an Exclude-family action stops
 *     evaluation — see the four Exclude* action types),
 *   - the structural `params` schema consumed by the editor UI,
 *   - the `userAuthorable` visibility flag used by the action-type picker
 *     to filter out actions that are registered for back-compat / loader
 *     completeness but should not appear in the new-rule wizard, and
 *   - `category` / `displayOrder` / `tags` for UI grouping and consumer-side
 *     filtering.
 */
struct PHOSPHORRULES_EXPORT ActionDescriptor
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
    /// Terminal actions (the Exclude family: Exclude / ExcludePlacement /
    /// ExcludeAnimations / ExcludeDecorations) stop evaluation once their
    /// rule matches — within the evaluator's terminal-action scope, see
    /// RuleEvaluator::setTerminalActionScope.
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
    /// Intended sort key within a category (lower = earlier). Advisory only
    /// today: the shipped picker sorts within a category alphabetically by
    /// label and no consumer reads this field.
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
class PHOSPHORRULES_EXPORT ActionRegistry
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

    /// True if @p action is a terminal action (one of the Exclude family).
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

    /// The wire param key whose schema `kind` is @p kind for @p type, or an
    /// empty string when @p type declares no such param (including an
    /// unregistered type).
    ///
    /// Lets a consumer ask a STRUCTURAL question about an action — "is this one
    /// scoped to an animation event?" — without maintaining its own list of
    /// type ids. A consumer that hardcodes such a list drifts silently the day
    /// a fourth action of the same shape is registered; this reads the same
    /// descriptor the editor renders from, so it cannot.
    ///
    /// The first match wins. No descriptor declares two params of one kind.
    QString paramKeyOfKind(const QString& type, QLatin1StringView kind) const;

private:
    ActionRegistry();
    void registerBuiltins();
    // registerBuiltins() is split across three TUs for file-size; these register
    // the descriptor-table thirds in order (engine/window-management/
    // animation/overlay, then per-window appearance/gap/autotile-param/
    // scrolling-param, then the tab- and drop-indicator families). Defined in
    // ruleaction_builtins_engine.cpp / ruleaction_builtins_appearance.cpp /
    // ruleaction_builtins_indicators.cpp.
    void registerBuiltinsEngine();
    void registerBuiltinsAppearance();
    void registerBuiltinsIndicators();

    QHash<QString, ActionDescriptor> m_descriptors;
};

/// True when @p actions carry at least one `Tag::Effect` action that can
/// actually take effect, delegating the event verdict to @p eventIsLive.
///
/// Splits one question in two so each half sits in the layer that can answer
/// it. The action walk is rule semantics and lives here: which actions are
/// appearance actions, which of those are scoped to an animation event, and
/// which param carries it — all read from the descriptor rather than from a
/// list of type ids that would drift the day a fourth event-scoped action is
/// registered. Whether a given event path can be driven per window is animation
/// taxonomy, which this library does not know, so the caller supplies it.
///
/// An action declaring no `animationEvent` param is not event-scoped at all (a
/// border, an opacity, a layer) and is live without consulting @p eventIsLive.
/// A null @p eventIsLive treats every event-scoped action as live, which is the
/// pre-narrowing behaviour.
///
/// The KWin window filter uses this to stop force-animating a window past the
/// user's own size and exclusion settings on behalf of a rule that could not
/// have changed anything.
/// Takes the action list rather than a Rule: this header defines RuleAction and
/// is included BY Rule.h, so the higher-level type is not visible here, and the
/// walk needs nothing else from a rule anyway.
PHOSPHORRULES_EXPORT bool ruleHasLiveEffectAction(const QList<RuleAction>& actions,
                                                  const std::function<bool(const QString& event)>& eventIsLive);

} // namespace PhosphorRules

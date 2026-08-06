// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

#include "MatchExpression.h"
#include "RuleAction.h"
#include "phosphorrules_export.h"

namespace PhosphorRules {

/**
 * @brief One semantic compatibility issue between a rule's match and an action.
 *
 * Issues are produced by @ref Rule::validationIssues — distinct from the
 * `isValid()` structural check, which only verifies that each action's params
 * pass its descriptor. A rule can be **valid** (every action registered, every
 * param well-formed) and still be **diagnosable** here: e.g. a `SetEngineMode`
 * (context-domain) action paired with a match that references a window
 * property silently never fires during context resolution.
 *
 * The loader keeps such rules — silently dropping a hand-edited rule is
 * hostile — but logs a warning per issue, and the settings UI surfaces them so
 * the user can see why a rule never fires. The picker UI uses the same domain
 * metadata to gray out incompatible action types up front.
 */
struct PHOSPHORRULES_EXPORT ValidationIssue
{
    /// Wire-stable code so callers can localise the message themselves without
    /// string-matching the diagnostic. New codes append; never renumber.
    enum class Code : int {
        /// A context-domain action (e.g. SetEngineMode / SetSnappingLayout /
        /// SetTilingAlgorithm / DisableEngine) paired with a match that
        /// references window-property fields. The match fails during context
        /// resolution (window fields are absent on the windowless query), so
        /// the action's slot is never filled.
        ContextActionWithWindowMatch = 0,
        /// The blanket `Exclude` on the same rule as one or more other
        /// slot-filling actions (border / opacity / animation override, but
        /// also gap / overlay / engine actions). `Exclude` is in every
        /// full-store evaluator's terminal scope, so it stops the resolve walk
        /// the moment it matches: the co-located action's slot may be dropped
        /// and lower-priority rules are suppressed for the window. Split the
        /// exclusion onto its own rule. The SCOPED exclusions
        /// (ExcludePlacement / ExcludeDecorations / ExcludeAnimations) are not
        /// flagged — an evaluator outside their slice skips them entirely
        /// (RuleEvaluator::setTerminalActionScope), so mixing one with an
        /// action of another domain is authorable. (Name kept for wire
        /// stability; the check is not limited to Tag::Effect actions.)
        TerminalActionWithEffectActions = 1,
        /// Two or more SAME-TYPE actions on one rule resolve to the same
        /// slot. Slot decoding is single-winner per (rule, type), so at most
        /// one of the duplicates takes effect (which one depends on the
        /// consumer's decode order) and the rest are dead weight. Distinct
        /// types sharing a slot (the SetSnappingLayout / SetTilingAlgorithm
        /// lossless pair) are NOT flagged. The flagged shape is what a buggy
        /// rule rebuild accretes, so surfacing it keeps such growth from
        /// surviving save/load silently.
        DuplicateSlotActions = 2,
        /// An action whose type is registered but whose params the descriptor
        /// rejects — most often a picker the author has not filled in yet
        /// (a rule template seeds an empty screen / layout / algorithm id).
        /// Such an action is DROPPED by `RuleAction::fromJson`, so saving the
        /// rule would silently lose it.
        ///
        /// Never produced by @ref Rule::validationIssues: a Rule already holds
        /// parsed actions, so a rejected payload cannot reach it. It is the
        /// settings editor's live check over the working rule's raw JSON,
        /// which is the only place the pre-parse shape exists. Declared here
        /// so the code vocabulary the UI switches on has one home.
        IncompleteActionPayload = 3,
    };

    Code code = Code::ContextActionWithWindowMatch;
    /// Index into the rule's `actions` list — points at the offending action.
    int actionIndex = -1;
    /// Action type id for diagnostics (avoids the caller re-indexing actions).
    QString actionType;
    /// English diagnostic suitable for logging. UI callers that need
    /// translation key off @ref code instead.
    QString message;

    bool operator==(const ValidationIssue& other) const
    {
        return code == other.code && actionIndex == other.actionIndex && actionType == other.actionType
            && message == other.message;
    }
};

/**
 * @brief One rule — `{ id, name, enabled, priority, match, actions, managed }`.
 *
 * A copyable value type living inside `RuleSet`'s ordered list. The
 * `id` is a stable QUuid (serialized `toString()` with braces). `priority`
 * orders evaluation: higher priority is evaluated first; ties break by the
 * rule set's list order.
 */
struct PHOSPHORRULES_EXPORT Rule
{
    QUuid id;
    QString name;
    bool enabled = true;
    int priority = 0;
    MatchExpression match; ///< default-constructs to the catch-all All{}
    QList<RuleAction> actions;
    /// True for built-in rules the application owns rather than the user
    /// (currently the baseline appearance rule). Managed rules are seeded and
    /// kept present by the store, are non-deletable and non-reorderable in the
    /// settings UI, and are pinned to lowest precedence so any user rule
    /// overrides them. The flag is metadata only — evaluation treats a managed
    /// rule like any other; the UI and store layers enforce the lifecycle.
    bool managed = false;

    /// True if the rule has a non-null id, a valid match expression, and
    /// every action validates against the registry.
    bool isValid() const;

    /// True if any of this rule's actions is terminal (one of the Exclude
    /// family — Exclude / ExcludePlacement / ExcludeAnimations /
    /// ExcludeDecorations).
    bool hasTerminalAction() const;

    /**
     * @brief Semantic compatibility issues between @ref match and each action.
     *
     * Distinct from @ref isValid — that one is the structural check
     * (registry-known type, well-formed params). This pass cross-checks the
     * action's @ref ActionDomain against the match expression's domain and
     * surfaces combinations that compile and load but silently never fire.
     *
     * Produces three codes:
     *  - @ref ValidationIssue::Code::ContextActionWithWindowMatch — a
     *    context-domain action paired with a match that references any
     *    window-property field. Detected via
     *    `MatchExpression::isContextOnly()`; an empty catch-all match is
     *    context-only and so is compatible.
     *  - @ref ValidationIssue::Code::TerminalActionWithEffectActions — a terminal
     *    action (any of the Exclude family) co-located with any non-terminal
     *    slot-filling action, which the terminal action's early-out may drop.
     *  - @ref ValidationIssue::Code::DuplicateSlotActions — two same-type
     *    actions on the same rule resolving to the same slot; slot decoding
     *    is single-winner per type, so all but one duplicate are dead weight.
     *
     * An empty list means no issues — the rule is well-formed at both layers.
     */
    QList<ValidationIssue> validationIssues() const;

    bool operator==(const Rule& other) const;
    bool operator!=(const Rule& other) const
    {
        return !(*this == other);
    }

    QJsonObject toJson() const;

    /// Strict loader — drops the rule (returns nullopt) on a missing/invalid
    /// id, a malformed match expression, or if every action fails to load.
    /// Individual malformed actions are dropped with a logged diagnostic.
    static std::optional<Rule> fromJson(const QJsonObject& obj);
};

} // namespace PhosphorRules

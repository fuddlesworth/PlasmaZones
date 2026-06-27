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
        /// A context-domain action (SetEngineMode / SetSnappingLayout /
        /// SetTilingAlgorithm / DisableEngine) paired with a match that
        /// references window-property fields. The match fails during context
        /// resolution (window fields are absent on the windowless query), so
        /// the action's slot is never filled.
        ContextActionWithWindowMatch = 0,
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
 * @brief One rule — `{ id, name, enabled, priority, match, actions }`.
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

    /// True if any of this rule's actions is terminal (Exclude).
    bool hasTerminalAction() const;

    /**
     * @brief Semantic compatibility issues between @ref match and each action.
     *
     * Distinct from @ref isValid — that one is the structural check
     * (registry-known type, well-formed params). This pass cross-checks the
     * action's @ref ActionDomain against the match expression's domain and
     * surfaces combinations that compile and load but silently never fire.
     *
     * Currently produces one code:
     *  - @ref ValidationIssue::Code::ContextActionWithWindowMatch — a
     *    context-domain action paired with a match that references any
     *    window-property field. Detected via
     *    `MatchExpression::isContextOnly()`; an empty catch-all match is
     *    context-only and so is compatible.
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

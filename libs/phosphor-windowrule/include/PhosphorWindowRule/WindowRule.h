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
#include "phosphorwindowrule_export.h"

namespace PhosphorWindowRule {

/**
 * @brief One window rule — `{ id, name, enabled, priority, match, actions }`.
 *
 * A copyable value type living inside `WindowRuleSet`'s ordered list. The
 * `id` is a stable QUuid (serialized `toString()` with braces). `priority`
 * orders evaluation: higher priority is evaluated first; ties break by the
 * rule set's list order.
 */
struct PHOSPHORWINDOWRULE_EXPORT WindowRule
{
    QUuid id;
    QString name;
    bool enabled = true;
    int priority = 0;
    MatchExpression match; ///< default-constructs to the catch-all All{}
    QList<RuleAction> actions;

    /// True if the rule has a non-null id, a valid match expression, and
    /// every action validates against the registry.
    bool isValid() const;

    /// True if any of this rule's actions is terminal (Exclude).
    bool hasTerminalAction() const;

    bool operator==(const WindowRule& other) const;
    bool operator!=(const WindowRule& other) const
    {
        return !(*this == other);
    }

    QJsonObject toJson() const;

    /// Strict loader — drops the rule (returns nullopt) on a missing/invalid
    /// id, a malformed match expression, or if every action fails to load.
    /// Individual malformed actions are dropped with a logged diagnostic.
    static std::optional<WindowRule> fromJson(const QJsonObject& obj);
};

} // namespace PhosphorWindowRule

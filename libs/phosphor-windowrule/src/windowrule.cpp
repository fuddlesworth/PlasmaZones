// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/WindowRule.h>

#include <QJsonArray>
#include <QJsonValue>

#include "windowrulelogging.h"

namespace PhosphorWindowRule {

namespace {

constexpr QLatin1StringView kKeyId{"id"};
constexpr QLatin1StringView kKeyName{"name"};
constexpr QLatin1StringView kKeyEnabled{"enabled"};
constexpr QLatin1StringView kKeyPriority{"priority"};
constexpr QLatin1StringView kKeyMatch{"match"};
constexpr QLatin1StringView kKeyActions{"actions"};

} // namespace

bool WindowRule::isValid() const
{
    if (id.isNull()) {
        return false;
    }
    if (!match.isValid()) {
        return false;
    }
    for (const RuleAction& action : actions) {
        if (!ActionRegistry::instance().validate(action)) {
            return false;
        }
    }
    return true;
}

bool WindowRule::hasTerminalAction() const
{
    for (const RuleAction& action : actions) {
        if (ActionRegistry::instance().isTerminal(action)) {
            return true;
        }
    }
    return false;
}

bool WindowRule::operator==(const WindowRule& other) const
{
    return id == other.id && name == other.name && enabled == other.enabled && priority == other.priority
        && match == other.match && actions == other.actions;
}

QJsonObject WindowRule::toJson() const
{
    QJsonObject o;
    // QUuid::toString() emits braces — the project convention for everything
    // except filesystem paths.
    o.insert(kKeyId, id.toString());
    o.insert(kKeyName, name);
    o.insert(kKeyEnabled, enabled);
    o.insert(kKeyPriority, priority);
    o.insert(kKeyMatch, match.toJson());
    QJsonArray actionsArr;
    for (const RuleAction& action : actions) {
        actionsArr.append(action.toJson());
    }
    o.insert(kKeyActions, actionsArr);
    return o;
}

std::optional<WindowRule> WindowRule::fromJson(const QJsonObject& obj)
{
    WindowRule rule;

    rule.id = QUuid::fromString(obj.value(kKeyId).toString());
    if (rule.id.isNull()) {
        qCWarning(lcWindowRule) << "Window rule has a missing/invalid id — dropping rule. name:"
                                << obj.value(kKeyName).toString();
        return std::nullopt;
    }
    rule.name = obj.value(kKeyName).toString();
    // `enabled` defaults to true when absent — a rule with no flag is on.
    rule.enabled = obj.value(kKeyEnabled).toBool(true);
    rule.priority = obj.value(kKeyPriority).toInt(0);

    const QJsonValue matchValue = obj.value(kKeyMatch);
    if (!matchValue.isObject()) {
        qCWarning(lcWindowRule) << "Window rule has a non-object `match` — dropping rule. id:" << rule.id.toString();
        return std::nullopt;
    }
    const auto match = MatchExpression::fromJson(matchValue.toObject());
    if (!match) {
        qCWarning(lcWindowRule) << "Window rule has a malformed `match` expression — dropping rule. id:"
                                << rule.id.toString();
        return std::nullopt;
    }
    rule.match = *match;

    // Individual malformed actions are dropped (with a diagnostic) rather
    // than dropping the whole rule — a rule may still be partly useful.
    const QJsonValue actionsValue = obj.value(kKeyActions);
    if (actionsValue.isArray()) {
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (!v.isObject()) {
                qCWarning(lcWindowRule) << "Window rule action is not an object — dropping action. rule id:"
                                        << rule.id.toString();
                continue;
            }
            const auto action = RuleAction::fromJson(v.toObject());
            if (!action) {
                continue;
            }
            rule.actions.append(*action);
        }
    }
    // A rule with zero loadable actions is inert — drop it so the set stays
    // free of dead weight, mirroring the strict-loader discipline.
    if (rule.actions.isEmpty()) {
        qCWarning(lcWindowRule) << "Window rule has no valid actions — dropping rule. id:" << rule.id.toString();
        return std::nullopt;
    }
    return rule;
}

} // namespace PhosphorWindowRule

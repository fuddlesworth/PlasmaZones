// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRules/WindowRule.h>

#include <QJsonArray>
#include <QJsonValue>

#include "windowrulelogging.h"

namespace PhosphorWindowRules {

namespace {

constexpr QLatin1StringView kKeyId{"id"};
constexpr QLatin1StringView kKeyName{"name"};
constexpr QLatin1StringView kKeyEnabled{"enabled"};
constexpr QLatin1StringView kKeyPriority{"priority"};
constexpr QLatin1StringView kKeyMatch{"match"};
constexpr QLatin1StringView kKeyActions{"actions"};
constexpr QLatin1StringView kKeyManaged{"managed"};

} // namespace

bool WindowRule::isValid() const
{
    if (id.isNull()) {
        return false;
    }
    if (!match.isValid()) {
        return false;
    }
    // Reject zero-action rules — `WindowRule::fromJson` already drops them
    // on load (a rule with no actions cannot fill any slot, so it is dead
    // weight in the priority-order walk). Mirroring the loader's predicate
    // here closes the door on a programmatic path that could put a
    // zero-action rule into the store via `setRules`/`addRule`; without
    // the check the rule lives in memory until the next save/load
    // round-trip silently drops it.
    if (actions.isEmpty()) {
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

QList<ValidationIssue> WindowRule::validationIssues() const
{
    QList<ValidationIssue> issues;

    // Compute the match's domain once — context-only iff every leaf references
    // a context field (ScreenId / VirtualDesktop / Activity). The catch-all is
    // context-only by this definition and so is compatible with every action.
    const bool matchIsContextOnly = match.isContextOnly();

    for (int i = 0; i < actions.size(); ++i) {
        const RuleAction& action = actions.at(i);
        const ActionDomain domain = ActionRegistry::instance().domainFor(action);
        if (domain == ActionDomain::Context && !matchIsContextOnly) {
            // The action fills a slot consumed during context resolution, but
            // the match references a window-property field — that leaf
            // evaluates false on the windowless context query, so the action
            // silently never fires. Flag the pairing.
            ValidationIssue issue;
            issue.code = ValidationIssue::Code::ContextActionWithWindowMatch;
            issue.actionIndex = i;
            issue.actionType = action.type;
            issue.message = QStringLiteral(
                                "Action `%1` is a context-mode action but the rule's match references window-property "
                                "fields, so the action never fires during context resolution.")
                                .arg(action.type);
            issues.append(issue);
        }
    }
    return issues;
}

bool WindowRule::operator==(const WindowRule& other) const
{
    return id == other.id && name == other.name && enabled == other.enabled && priority == other.priority
        && match == other.match && actions == other.actions && managed == other.managed;
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
    // Only emit `managed` for the built-in rules it applies to — user rules
    // (the overwhelming majority) stay free of the key, and its absence loads
    // back as false.
    if (managed) {
        o.insert(kKeyManaged, true);
    }
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
    rule.managed = obj.value(kKeyManaged).toBool(false);

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

} // namespace PhosphorWindowRules

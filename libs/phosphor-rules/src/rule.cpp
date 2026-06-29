// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/Rule.h>

#include <QJsonArray>
#include <QJsonValue>

#include "rulelogging.h"

namespace PhosphorRules {

namespace {

constexpr QLatin1StringView kKeyId{"id"};
constexpr QLatin1StringView kKeyName{"name"};
constexpr QLatin1StringView kKeyEnabled{"enabled"};
constexpr QLatin1StringView kKeyPriority{"priority"};
constexpr QLatin1StringView kKeyMatch{"match"};
constexpr QLatin1StringView kKeyActions{"actions"};
constexpr QLatin1StringView kKeyManaged{"managed"};

} // namespace

bool Rule::isValid() const
{
    if (id.isNull()) {
        return false;
    }
    if (!match.isValid()) {
        return false;
    }
    // Reject zero-action rules — `Rule::fromJson` already drops them
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

bool Rule::hasTerminalAction() const
{
    for (const RuleAction& action : actions) {
        if (ActionRegistry::instance().isTerminal(action)) {
            return true;
        }
    }
    return false;
}

QList<ValidationIssue> Rule::validationIssues() const
{
    QList<ValidationIssue> issues;

    // Compute the match's domain once — context-only iff every leaf references
    // a context field (ScreenId / VirtualDesktop / Activity / Mode / TiledWindowCount).
    // The catch-all is context-only by this definition and so is compatible with
    // every action.
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

    // A terminal action (Exclude / ExcludeAnimations) co-located with any
    // non-terminal slot-filling action: a terminal action stops the evaluator's
    // resolve walk the moment it matches, so any other action on the same rule
    // may be dropped (the appearance/animation evaluator drops border / opacity /
    // animation slots; the daemon context evaluator drops gap / overlay / engine
    // slots) and lower-priority rules are suppressed for the window. Flag each
    // co-located non-terminal action so the author splits the exclusion onto its
    // own rule.
    if (hasTerminalAction()) {
        const ActionRegistry& registry = ActionRegistry::instance();
        for (int i = 0; i < actions.size(); ++i) {
            const RuleAction& action = actions.at(i);
            if (registry.isTerminal(action)) {
                continue; // the terminal action itself is the intended effect
            }
            ValidationIssue issue;
            issue.code = ValidationIssue::Code::TerminalActionWithEffectActions;
            issue.actionIndex = i;
            issue.actionType = action.type;
            issue.message = QStringLiteral(
                                "Action `%1` may not take effect: the rule also has a terminal action "
                                "(Exclude / ExcludeAnimations) that stops resolution before later actions apply. "
                                "Put the exclusion on a separate rule.")
                                .arg(action.type);
            issues.append(issue);
        }
    }
    return issues;
}

bool Rule::operator==(const Rule& other) const
{
    return id == other.id && name == other.name && enabled == other.enabled && priority == other.priority
        && match == other.match && actions == other.actions && managed == other.managed;
}

QJsonObject Rule::toJson() const
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

std::optional<Rule> Rule::fromJson(const QJsonObject& obj)
{
    Rule rule;

    rule.id = QUuid::fromString(obj.value(kKeyId).toString());
    if (rule.id.isNull()) {
        qCWarning(lcRule) << "Rule has a missing/invalid id — dropping rule. name:" << obj.value(kKeyName).toString();
        return std::nullopt;
    }
    rule.name = obj.value(kKeyName).toString();
    // `enabled` defaults to true when absent — a rule with no flag is on.
    rule.enabled = obj.value(kKeyEnabled).toBool(true);
    rule.priority = obj.value(kKeyPriority).toInt(0);
    rule.managed = obj.value(kKeyManaged).toBool(false);

    const QJsonValue matchValue = obj.value(kKeyMatch);
    if (!matchValue.isObject()) {
        qCWarning(lcRule) << "Rule has a non-object `match` — dropping rule. id:" << rule.id.toString();
        return std::nullopt;
    }
    const auto match = MatchExpression::fromJson(matchValue.toObject());
    if (!match) {
        qCWarning(lcRule) << "Rule has a malformed `match` expression — dropping rule. id:" << rule.id.toString();
        return std::nullopt;
    }
    rule.match = *match;

    // Individual malformed actions are dropped (with a diagnostic) rather
    // than dropping the whole rule — a rule may still be partly useful.
    const QJsonValue actionsValue = obj.value(kKeyActions);
    if (actionsValue.isArray()) {
        for (const QJsonValue& v : actionsValue.toArray()) {
            if (!v.isObject()) {
                qCWarning(lcRule) << "Rule action is not an object — dropping action. rule id:" << rule.id.toString();
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
        qCWarning(lcRule) << "Rule has no valid actions — dropping rule. id:" << rule.id.toString();
        return std::nullopt;
    }
    return rule;
}

} // namespace PhosphorRules

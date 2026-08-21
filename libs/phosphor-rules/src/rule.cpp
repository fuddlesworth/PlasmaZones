// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/Rule.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>

#include "rulelogging.h"

#include <algorithm>

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
    // a context field, i.e. a `FieldSource::Context` row of `kFieldTable`
    // (currently ScreenId / VirtualDesktop / Activity / Mode / TiledWindowCount /
    // ScreenOrientation / ActiveLayout / ColorScheme). The catch-all is context-only by this
    // definition and so is compatible with every action.
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

    // The BLANKET Exclude co-located with any other slot-filling action: it
    // stops the evaluator's resolve walk the moment it matches, so any other
    // action on the same rule may be dropped (the appearance/animation
    // evaluator drops border / opacity / animation slots; the daemon context
    // evaluator drops gap / overlay / engine slots) and lower-priority rules
    // are suppressed for the window. Flag each co-located action so the author
    // splits the exclusion onto its own rule.
    //
    // The blanket Exclude cancels EVERY sibling: it is in every full-store
    // evaluator's scope. The scoped exclusions cancel only the siblings that
    // the one evaluator honouring them also resolves (RuleEvaluator's
    // setTerminalActionScope skips an out-of-scope terminal action entirely),
    // and the second pass below flags exactly that intersection — see its
    // comment for the per-scope mapping.
    const bool hasBlanketExclude = std::any_of(actions.cbegin(), actions.cend(), [](const RuleAction& a) {
        return a.type == ActionType::Exclude;
    });
    if (hasBlanketExclude) {
        for (int i = 0; i < actions.size(); ++i) {
            const RuleAction& action = actions.at(i);
            if (action.type == ActionType::Exclude) {
                continue; // the exclusion itself is the intended effect
            }
            ValidationIssue issue;
            issue.code = ValidationIssue::Code::TerminalActionWithEffectActions;
            issue.actionIndex = i;
            issue.actionType = action.type;
            issue.message = QStringLiteral(
                                "Action `%1` may not take effect: the rule also has a terminal exclusion action "
                                "that stops the rest of the rule from applying. "
                                "Put the exclusion on a separate rule.")
                                .arg(action.type);
            issues.append(issue);
        }
    }

    // The SCOPED exclusions are walk-stoppers only inside the evaluator bound
    // to their slice, but that evaluator resolves real slots of its own, so a
    // scoped exclusion still cancels the co-located actions that SAME
    // evaluator would have resolved:
    //  - ExcludeAnimations rides the effect's animation/appearance evaluator
    //    (scope {Exclude, ExcludeAnimations}), which resolves the Tag::Effect
    //    actions — border, opacity, stacking layer, animation overrides.
    //  - ExcludePlacement rides the daemon's window-tracking evaluator (scope
    //    {Exclude, ExcludePlacement}), which resolves the window-domain
    //    placement/routing/open/restore slots — the slot-filling
    //    window-domain actions the COMPOSITOR does not consume.
    //  - ExcludeDecorations' slice resolves no other slot, so it mixes freely.
    // The Tag::EffectVerdict actions (OpenFullscreen, ScrollFactor) are
    // cancelled by NEITHER scoped exclusion: they are delivered to the
    // compositor, so the daemon's placement evaluator never resolves them, and
    // their own effect-side evaluator scopes terminal actions to the blanket
    // Exclude alone, so ExcludeAnimations does not stop its walk. That is the
    // whole point of the separate tag — see Tag::EffectVerdict in RuleAction.h.
    // Context-domain slots (gaps, overlays, assignments, scroll knobs) resolve
    // under a {Exclude}-only evaluator, so a scoped exclusion beside them is
    // inert-and-honoured, not cancelling — they stay unflagged, which is what
    // keeps the scoped split authorable.
    if (!hasBlanketExclude) {
        const bool hasExcludeAnimations = std::any_of(actions.cbegin(), actions.cend(), [](const RuleAction& a) {
            return a.type == ActionType::ExcludeAnimations;
        });
        const bool hasExcludePlacement = std::any_of(actions.cbegin(), actions.cend(), [](const RuleAction& a) {
            return a.type == ActionType::ExcludePlacement;
        });
        if (hasExcludeAnimations || hasExcludePlacement) {
            const ActionRegistry& registry = ActionRegistry::instance();
            for (int i = 0; i < actions.size(); ++i) {
                const RuleAction& action = actions.at(i);
                if (registry.isTerminal(action)) {
                    continue; // exclusions themselves are the intended effect
                }
                // Two distinct questions, so two distinct tag reads: whether
                // the ANIMATION evaluator resolves the action (Tag::Effect
                // alone — its scope is what ExcludeAnimations stops), and
                // whether the COMPOSITOR consumes it at all (either effect
                // tag), which is what keeps it out of the daemon placement
                // evaluator ExcludePlacement stops.
                const bool animationResolved = registry.hasTag(action.type, Tag::Effect);
                const bool compositorConsumed = animationResolved || registry.hasTag(action.type, Tag::EffectVerdict);
                const bool cancelled = (hasExcludeAnimations && animationResolved)
                    || (hasExcludePlacement && !compositorConsumed && registry.domainFor(action) == ActionDomain::Window
                        && !registry.slotFor(action).isEmpty());
                if (!cancelled) {
                    continue;
                }
                ValidationIssue issue;
                issue.code = ValidationIssue::Code::TerminalActionWithEffectActions;
                issue.actionIndex = i;
                issue.actionType = action.type;
                issue.message = QStringLiteral(
                                    "Action `%1` may not take effect: the rule also has a terminal exclusion action "
                                    "that stops the rest of the rule from applying. "
                                    "Put the exclusion on a separate rule.")
                                    .arg(action.type);
                issues.append(issue);
            }
        }
    }

    // Duplicate slot fill: two SAME-TYPE actions on one rule resolving to the
    // SAME slot. Slot decoding is single-winner per (rule, type), so at most
    // one duplicate takes effect (which one depends on the consumer's decode
    // order) and the rest are dead weight — the exact shape a buggy rule
    // rebuild accretes. The key is (slot, type), NOT slot alone: distinct
    // types deliberately share a slot (SetSnappingLayout and
    // SetTilingAlgorithm both fill the layout slot — the lossless
    // mode-toggle pair — and the active mode picks between them), and the
    // animation actions' event-scoped resolvers separate legitimate
    // same-type pairs into distinct slots. A repeated SetAlgorithmParam IS
    // flagged — its slot is constant, and the context resolver reads it
    // single-winner, so the second copy really is dead. Unresolvable
    // actions (empty slot) are skipped — the structural isValid() pass
    // already rejects those.
    QHash<QPair<QString, QString>, int> firstActionBySlotAndType;
    for (int i = 0; i < actions.size(); ++i) {
        const RuleAction& action = actions.at(i);
        const QString slot = ActionRegistry::instance().slotFor(action);
        if (slot.isEmpty()) {
            continue;
        }
        const auto key = qMakePair(slot, action.type);
        const auto it = firstActionBySlotAndType.constFind(key);
        if (it == firstActionBySlotAndType.constEnd()) {
            firstActionBySlotAndType.insert(key, i);
            continue;
        }
        ValidationIssue issue;
        issue.code = ValidationIssue::Code::DuplicateSlotActions;
        issue.actionIndex = i;
        issue.actionType = action.type;
        issue.message = QStringLiteral(
                            "Action `%1` fills slot `%2`, which an earlier action of the same type "
                            "on this rule already fills; only one of them takes effect.")
                            .arg(action.type, slot);
        issues.append(issue);
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

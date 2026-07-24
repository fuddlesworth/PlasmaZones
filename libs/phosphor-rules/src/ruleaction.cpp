// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRules/RuleAction.h>

#include <QJsonArray>
#include <QJsonValue>

#include "rulelogging.h"
#include "ruleaction_builtins_p.h"

#include <algorithm>

namespace PhosphorRules {

namespace {

constexpr QLatin1StringView kKeyType{"type"};
// Action params are stored inline alongside `type` in the action object; the
// loader strips `type` and treats the remainder as `params`.

} // namespace

// ── RuleAction (de)serialization ────────────────────────────────────────

QJsonObject RuleAction::toJson() const
{
    // `params` are written inline alongside `type`, so `"type"` is a RESERVED
    // param key: a `params` entry keyed `"type"` is overwritten by `insert`
    // here (and `fromJson` strips it back out on load). A free-form
    // `acceptAny` action must never carry a user `"type"` param — it would be
    // silently clobbered. The strict-key check rejects it for any descriptor
    // with a non-empty `allowedKeys`, but free-form descriptors opt out.
    if (params.contains(kKeyType)) {
        // A free-form (`acceptAny`) action whose params carry a `"type"` key
        // would have it silently clobbered by the insert below. Log at debug
        // level — the clobber is documented in the RuleAction.h header
        // comments on `params` and would otherwise re-emit on every save of
        // every store reload. `qCWarning` here turned routine persistence
        // cycles into recurring log noise even for correctly-loaded rules.
        qCDebug(lcRule) << "RuleAction::toJson: params carry a reserved `type` key — it will be clobbered by "
                           "the action type. action type:"
                        << type;
    }
    QJsonObject o = params;
    o.insert(kKeyType, type);
    return o;
}

std::optional<RuleAction> RuleAction::fromJson(const QJsonObject& obj)
{
    const QString type = obj.value(kKeyType).toString();
    if (type.isEmpty()) {
        qCWarning(lcRule) << "Action object has no `type` — dropping action.";
        return std::nullopt;
    }
    if (!ActionRegistry::instance().isRegistered(type)) {
        qCWarning(lcRule) << "Action type is not registered — dropping action. type:" << type;
        return std::nullopt;
    }
    RuleAction action;
    action.type = type;
    action.params = obj;
    action.params.remove(kKeyType);

    // Strict-key discipline — reject an action carrying a param key the
    // descriptor does not declare. An unknown key is almost always a typo or
    // a stale-schema payload; silently retaining it would let a misspelled
    // key sit inertly in the rule store forever. A descriptor with an empty
    // `allowedKeys` set opts out (free-form params).
    const auto descriptor = ActionRegistry::instance().descriptor(type);
    if (descriptor && !descriptor->allowedKeys.isEmpty()) {
        // Iterate via const-iterator rather than `keys()` to avoid the
        // per-call QStringList allocation `QJsonObject::keys()` does.
        // The per-key `allowedKeys.contains(...)` lookup is O(M) over a
        // QStringList — acceptable at the built-in scale (M ≤ 3 allowed
        // keys × K ≤ 3 params = 9 string compares per action load) and
        // faster in practice than the QSet alternative at these sizes.
        // If a future descriptor grows allowedKeys beyond a handful,
        // switch the field type to QSet<QString> in ActionDescriptor.
        for (auto it = action.params.constBegin(); it != action.params.constEnd(); ++it) {
            if (!descriptor->allowedKeys.contains(it.key())) {
                qCWarning(lcRule) << "Action params carry an unexpected key — dropping action. type:" << type
                                  << "key:" << it.key();
                return std::nullopt;
            }
        }
    }

    if (!ActionRegistry::instance().validate(action)) {
        qCWarning(lcRule) << "Action params failed descriptor validation — dropping action. type:" << type;
        return std::nullopt;
    }
    return action;
}

// ── ActionRegistry ──────────────────────────────────────────────────────

ActionRegistry& ActionRegistry::instance()
{
    static ActionRegistry registry;
    return registry;
}

ActionRegistry::ActionRegistry()
{
    registerBuiltins();
}

void ActionRegistry::registerAction(const ActionDescriptor& descriptor)
{
    m_descriptors.insert(descriptor.type, descriptor);
}

bool ActionRegistry::unregisterAction(const QString& type)
{
    return m_descriptors.remove(type) > 0;
}

bool ActionRegistry::isRegistered(const QString& type) const
{
    return m_descriptors.contains(type);
}

std::optional<ActionDescriptor> ActionRegistry::descriptor(const QString& type) const
{
    const auto it = m_descriptors.constFind(type);
    if (it == m_descriptors.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

QString ActionRegistry::slotFor(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd() || !it->slotFor) {
        return QString();
    }
    return it->slotFor(action.params);
}

bool ActionRegistry::isTerminal(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    return it != m_descriptors.constEnd() && it->terminal;
}

bool ActionRegistry::validate(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd()) {
        return false;
    }
    if (it->validate && !it->validate(action.params)) {
        return false;
    }
    // A descriptor that resolves to no slot (and is not terminal) cannot
    // contribute to a resolution — treat it as invalid.
    if (!it->terminal && (!it->slotFor || it->slotFor(action.params).isEmpty())) {
        return false;
    }
    return true;
}

ActionDomain ActionRegistry::domainFor(const RuleAction& action) const
{
    const auto it = m_descriptors.constFind(action.type);
    if (it == m_descriptors.constEnd()) {
        return ActionDomain::Window;
    }
    return it->domain;
}

QStringList ActionRegistry::registeredTypes() const
{
    return m_descriptors.keys();
}

bool ActionRegistry::hasTag(const QString& type, QLatin1StringView tag) const
{
    const auto it = m_descriptors.constFind(type);
    if (it == m_descriptors.constEnd()) {
        return false;
    }
    // Compare against QLatin1StringView directly (QString::operator== has a
    // non-allocating overload) rather than materialising a throwaway QString.
    return std::any_of(it->tags.cbegin(), it->tags.cend(), [tag](const QString& t) {
        return t == tag;
    });
}

QStringList ActionRegistry::typesWithTag(QLatin1StringView tag) const
{
    QStringList result;
    const QString tagStr(tag);
    for (auto it = m_descriptors.constBegin(); it != m_descriptors.constEnd(); ++it) {
        if (it->tags.contains(tagStr)) {
            result.append(it.key());
        }
    }
    return result;
}

void ActionRegistry::registerBuiltins()
{
    // Split across two TUs for file-size; the registration order (engine half
    // first, then appearance half) is load-bearing for the descriptors'
    // displayOrder / category grouping, so the call order here is fixed.
    registerBuiltinsEngine();
    registerBuiltinsAppearance();
}

} // namespace PhosphorRules

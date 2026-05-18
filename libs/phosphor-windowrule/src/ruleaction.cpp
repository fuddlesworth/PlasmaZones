// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleAction.h>

#include <QJsonValue>

#include "windowrulelogging.h"

namespace PhosphorWindowRule {

namespace {

constexpr QLatin1StringView kKeyType{"type"};
// Action params are stored inline alongside `type` in the action object; the
// loader strips `type` and treats the remainder as `params`.

/// A descriptor whose params payload carries no constraint — accepts any
/// object. Used for actions whose params are free-form or future-extensible.
bool acceptAny(const QJsonObject&)
{
    return true;
}

/// Validates that @p params has a non-empty string at @p key.
bool hasNonEmptyString(const QJsonObject& params, QLatin1StringView key)
{
    const QJsonValue v = params.value(key);
    return v.isString() && !v.toString().isEmpty();
}

} // namespace

// ── RuleAction (de)serialization ────────────────────────────────────────

QJsonObject RuleAction::toJson() const
{
    // `params` are written inline alongside `type` — `type` is reserved.
    QJsonObject o = params;
    o.insert(kKeyType, type);
    return o;
}

std::optional<RuleAction> RuleAction::fromJson(const QJsonObject& obj)
{
    const QString type = obj.value(kKeyType).toString();
    if (type.isEmpty()) {
        qCWarning(lcWindowRule) << "Action object has no `type` — dropping action.";
        return std::nullopt;
    }
    if (!ActionRegistry::instance().isRegistered(type)) {
        qCWarning(lcWindowRule) << "Action type is not registered — dropping action. type:" << type;
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
        for (const QString& key : action.params.keys()) {
            if (!descriptor->allowedKeys.contains(key)) {
                qCWarning(lcWindowRule) << "Action params carry an unexpected key — dropping action. type:" << type
                                        << "key:" << key;
                return std::nullopt;
            }
        }
    }

    if (!ActionRegistry::instance().validate(action)) {
        qCWarning(lcWindowRule) << "Action params failed descriptor validation — dropping action. type:" << type;
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

QStringList ActionRegistry::registeredTypes() const
{
    return m_descriptors.keys();
}

void ActionRegistry::registerBuiltins()
{
    // ── engine-mode slot ──
    registerAction(ActionDescriptor{QString(ActionType::SetEngineMode),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::EngineMode);
                                    },
                                    [](const QJsonObject& p) {
                                        return hasNonEmptyString(p, QLatin1StringView("mode"));
                                    },
                                    false, QStringList{QStringLiteral("mode")}});

    // ── layout slot — both layout-shaping actions share it ──
    registerAction(ActionDescriptor{QString(ActionType::SetSnappingLayout),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::Layout);
                                    },
                                    [](const QJsonObject& p) {
                                        return hasNonEmptyString(p, QLatin1StringView("layoutId"));
                                    },
                                    false, QStringList{QStringLiteral("layoutId")}});
    registerAction(ActionDescriptor{QString(ActionType::SetTilingAlgorithm),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::Layout);
                                    },
                                    [](const QJsonObject& p) {
                                        return hasNonEmptyString(p, QLatin1StringView("algorithm"));
                                    },
                                    false, QStringList{QStringLiteral("algorithm")}});

    // ── engine-enable slot ──
    registerAction(ActionDescriptor{QString(ActionType::DisableEngine),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::EngineEnable);
                                    },
                                    &acceptAny, false, QStringList{QStringLiteral("mode")}});

    // ── manage slot — terminal. Exclude is intentionally free-form: an empty
    //    `allowedKeys` opts out of the strict-key check so a future Exclude
    //    reason/scope param can be added without a schema bump. ──
    registerAction(ActionDescriptor{QString(ActionType::Exclude),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::Manage);
                                    },
                                    &acceptAny, true, QStringList{}});

    // ── float slot — intentionally free-form (future float-geometry hints);
    //    empty `allowedKeys` opts out of the strict-key check. ──
    registerAction(ActionDescriptor{QString(ActionType::Float),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::Float);
                                    },
                                    &acceptAny, false, QStringList{}});

    // ── animation slots — event-scoped: "anim-shader:<event>" ──
    registerAction(ActionDescriptor{
        QString(ActionType::OverrideAnimationShader),
        [](const QJsonObject& p) -> QString {
            const QString event = p.value(QLatin1StringView("event")).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimShaderPrefix) + event;
        },
        [](const QJsonObject& p) {
            return hasNonEmptyString(p, QLatin1StringView("event"));
        },
        false, QStringList{QStringLiteral("event"), QStringLiteral("effectId"), QStringLiteral("params")}});
    registerAction(ActionDescriptor{
        QString(ActionType::OverrideAnimationTiming),
        [](const QJsonObject& p) -> QString {
            const QString event = p.value(QLatin1StringView("event")).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimTimingPrefix) + event;
        },
        [](const QJsonObject& p) {
            return hasNonEmptyString(p, QLatin1StringView("event"));
        },
        false, QStringList{QStringLiteral("event"), QStringLiteral("curve"), QStringLiteral("durationMs")}});

    // ── opacity slot ──
    registerAction(ActionDescriptor{QString(ActionType::SetOpacity),
                                    [](const QJsonObject&) {
                                        return QString(ActionSlot::Opacity);
                                    },
                                    [](const QJsonObject& p) {
                                        const QJsonValue v = p.value(QLatin1StringView("value"));
                                        return v.isDouble() && v.toDouble() >= 0.0 && v.toDouble() <= 1.0;
                                    },
                                    false, QStringList{QStringLiteral("value")}});
}

} // namespace PhosphorWindowRule

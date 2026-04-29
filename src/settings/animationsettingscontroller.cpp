// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationsettingscontroller.h"
#include "../config/settings.h"
#include "../pz_i18n.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

namespace PlasmaZones {

AnimationSettingsController::AnimationSettingsController(Settings* settings,
                                                         PhosphorAnimationShaders::AnimationShaderRegistry* registry,
                                                         QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_registry(registry)
{
    if (m_registry) {
        connect(m_registry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationSettingsController::effectsChanged);
    }
}

QVariantList AnimationSettingsController::availableTransitionEffects() const
{
    if (!m_registry)
        return {};

    QVariantList result;
    const auto effects = m_registry->availableEffects();
    for (const auto& e : effects) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), e.id);
        item.insert(QStringLiteral("name"), e.name);
        item.insert(QStringLiteral("description"), e.description);
        item.insert(QStringLiteral("category"), e.category);
        item.insert(QStringLiteral("author"), e.author);
        item.insert(QStringLiteral("isUserEffect"), e.isUserEffect);
        item.insert(QStringLiteral("parameterCount"), e.parameters.size());
        result.append(item);
    }
    return result;
}

QStringList AnimationSettingsController::eventPaths() const
{
    return PhosphorAnimation::ProfilePaths::allBuiltInPaths();
}

QString AnimationSettingsController::effectForPath(const QString& eventPath) const
{
    if (!m_settings)
        return {};
    const auto tree = m_settings->shaderProfileTree();
    const auto profile = tree.resolve(eventPath);
    return profile.effectiveEffectId();
}

void AnimationSettingsController::setEffectForPath(const QString& eventPath, const QString& effectId)
{
    if (!m_settings)
        return;
    auto tree = m_settings->shaderProfileTree();
    PhosphorAnimationShaders::ShaderProfile profile;
    profile.effectId = effectId;
    tree.setOverride(eventPath, profile);
    m_settings->setShaderProfileTree(tree);
}

void AnimationSettingsController::clearEffectForPath(const QString& eventPath)
{
    if (!m_settings)
        return;
    auto tree = m_settings->shaderProfileTree();
    tree.clearOverride(eventPath);
    m_settings->setShaderProfileTree(tree);
}

QVariantMap AnimationSettingsController::parametersForPath(const QString& eventPath) const
{
    if (!m_settings)
        return {};
    const auto tree = m_settings->shaderProfileTree();
    const auto profile = tree.resolve(eventPath);
    return profile.effectiveParameters();
}

void AnimationSettingsController::setParameterForPath(const QString& eventPath, const QString& paramId,
                                                      const QVariant& value)
{
    if (!m_settings)
        return;
    auto tree = m_settings->shaderProfileTree();
    auto existing = tree.directOverride(eventPath);
    QVariantMap params = existing.parameters.value_or(QVariantMap());
    params.insert(paramId, value);
    existing.parameters = params;
    if (!existing.effectId)
        existing.effectId = effectForPath(eventPath);
    tree.setOverride(eventPath, existing);
    m_settings->setShaderProfileTree(tree);
}

QVariantList AnimationSettingsController::shaderParameters(const QString& effectId) const
{
    if (!m_registry)
        return {};
    const auto e = m_registry->effect(effectId);
    QVariantList result;
    for (const auto& p : e.parameters) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), p.id);
        item.insert(QStringLiteral("name"), p.name);
        item.insert(QStringLiteral("type"), p.type);
        if (p.defaultValue.isValid())
            item.insert(QStringLiteral("default"), p.defaultValue);
        if (p.minValue.isValid())
            item.insert(QStringLiteral("min"), p.minValue);
        if (p.maxValue.isValid())
            item.insert(QStringLiteral("max"), p.maxValue);
        result.append(item);
    }
    return result;
}

QVariantMap AnimationSettingsController::shaderDefaults(const QString& effectId) const
{
    if (!m_registry)
        return {};
    const auto e = m_registry->effect(effectId);
    QVariantMap result;
    for (const auto& p : e.parameters) {
        if (p.defaultValue.isValid())
            result.insert(p.id, p.defaultValue);
    }
    return result;
}

QVariantMap AnimationSettingsController::effectMetadata(const QString& effectId) const
{
    if (!m_registry)
        return {};
    const auto e = m_registry->effect(effectId);
    if (e.id.isEmpty())
        return {};
    QVariantMap item;
    item.insert(QStringLiteral("id"), e.id);
    item.insert(QStringLiteral("name"), e.name);
    item.insert(QStringLiteral("description"), e.description);
    item.insert(QStringLiteral("category"), e.category);
    item.insert(QStringLiteral("author"), e.author);
    item.insert(QStringLiteral("version"), e.version);
    item.insert(QStringLiteral("isUserEffect"), e.isUserEffect);
    item.insert(QStringLiteral("fragmentShaderPath"), e.fragmentShaderPath);
    item.insert(QStringLiteral("previewPath"), e.previewPath);
    return item;
}

QString AnimationSettingsController::parentChainForEvent(const QString& eventPath) const
{
    QStringList chain;
    QString cursor = eventPath;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = PhosphorAnimation::ProfilePaths::parentPath(cursor);
    }

    const QHash<QString, QString> displayNames = {
        {QStringLiteral("global"), PzI18n::tr("Global")},
        {QStringLiteral("window"), PzI18n::tr("Window")},
        {QStringLiteral("window.open"), PzI18n::tr("Open")},
        {QStringLiteral("window.close"), PzI18n::tr("Close")},
        {QStringLiteral("zone"), PzI18n::tr("Zone")},
        {QStringLiteral("zone.snapIn"), PzI18n::tr("Snap In")},
        {QStringLiteral("zone.snapOut"), PzI18n::tr("Snap Out")},
        {QStringLiteral("zone.highlight"), PzI18n::tr("Highlight")},
        {QStringLiteral("zone.layoutSwitchIn"), PzI18n::tr("Layout Switch In")},
        {QStringLiteral("zone.layoutSwitchOut"), PzI18n::tr("Layout Switch Out")},
        {QStringLiteral("osd"), PzI18n::tr("OSD")},
        {QStringLiteral("osd.show"), PzI18n::tr("Show")},
        {QStringLiteral("osd.hide"), PzI18n::tr("Hide")},
        {QStringLiteral("panel"), PzI18n::tr("Panel")},
        {QStringLiteral("panel.popup"), PzI18n::tr("Popup")},
        {QStringLiteral("shader"), PzI18n::tr("Shader")},
        {QStringLiteral("workspace"), PzI18n::tr("Workspace")},
        {QStringLiteral("cursor"), PzI18n::tr("Cursor")},
    };

    QStringList display;
    for (const auto& p : chain) {
        display.append(displayNames.value(p, p));
    }
    return display.join(QStringLiteral(" → "));
}

QString AnimationSettingsController::inheritSummaryForEvent(const QString& eventPath) const
{
    if (!m_settings)
        return {};

    const auto shaderTree = m_settings->shaderProfileTree();
    const auto shaderProfile = shaderTree.resolve(eventPath);
    const QString effectId = shaderProfile.effectiveEffectId();

    if (effectId.isEmpty())
        return PzI18n::tr("No transition effect");

    if (m_registry && m_registry->hasEffect(effectId)) {
        const auto e = m_registry->effect(effectId);
        return e.name;
    }

    return effectId;
}

} // namespace PlasmaZones

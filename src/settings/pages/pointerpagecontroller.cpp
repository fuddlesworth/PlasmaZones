// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pointerpagecontroller.h"

#include "pointerpreviewcontroller.h"

#include "core/interfaces/isettings.h"

#include <PhosphorPointer/PointerProfile.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>

#include <QHash>
#include <QSet>

namespace PlasmaZones {

using PhosphorPointerShaders::PointerLayer;
using PhosphorPointerShaders::PointerProfile;
using PhosphorPointerShaders::PointerShaderEffect;

namespace {

/// One pack's declared parameters as ParameterEditor rows.
QVariantList parameterRows(const PointerShaderEffect& effect)
{
    QVariantList rows;
    rows.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.name);
        m.insert(QStringLiteral("type"), p.type);
        m.insert(QStringLiteral("description"), p.description);
        m.insert(QStringLiteral("group"), p.group);
        m.insert(QStringLiteral("default"), p.defaultValue);
        m.insert(QStringLiteral("min"), p.minValue);
        m.insert(QStringLiteral("max"), p.maxValue);
        m.insert(QStringLiteral("step"), p.stepValue);
        rows.append(m);
    }
    return rows;
}

/// Index of @p packId in @p profile, or -1. The chain holds a pack at most
/// once, so the first match is the only one.
int indexOfPack(const PointerProfile& profile, const QString& packId)
{
    for (int i = 0; i < profile.layers.size(); ++i) {
        if (profile.layers.at(i).effectId == packId) {
            return i;
        }
    }
    return -1;
}

} // namespace

PointerPageController::PointerPageController(PhosphorPointerShaders::PointerShaderRegistry* registry,
                                             ISettings* settings, QObject* parent)
    // "pointer-staging", not "pointer-chain": the sidebar nav node owns
    // the bare page id, and the staging controller stays independently
    // addressable — the same split DecorationPageController makes.
    : PhosphorControl::PageController(QStringLiteral("pointer-staging"), parent)
    , m_registry(registry)
    , m_settings(settings)
    , m_preview(new PointerPreviewController(registry, this))
{
    if (m_registry) {
        connect(m_registry, &PhosphorPointerShaders::PointerShaderRegistry::effectsChanged, this,
                &PointerPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // Re-fire chainChanged so the page rebinds after a global reload
        // (Discard / Settings::load()) as well as after our own mutators write
        // the profile back.
        connect(m_settings, &ISettings::pointerChainChanged, this, &PointerPageController::chainChanged);
        connect(m_settings, &ISettings::pointerEnabledChanged, this, &PointerPageController::enabledChanged);
    }
    // initSetsStore() wires chainChanged into the store's
    // notifyLiveStateChanged, so it must run after the connects above.
    initSetsStore();
}

PointerPageController::~PointerPageController() = default;

QObject* PointerPageController::previewController() const
{
    return m_preview;
}

QString PointerPageController::previewKind() const
{
    return QStringLiteral("pointer");
}

bool PointerPageController::enabled() const
{
    return m_settings && m_settings->pointerEnabled();
}

void PointerPageController::setEnabled(bool value)
{
    if (!m_settings) {
        return;
    }
    // No changed-check here on purpose: Settings::setPointerEnabled already
    // gates its own NOTIFY on a real change, and enabledChanged is re-emitted
    // from that signal in the constructor.
    m_settings->setPointerEnabled(value);
}

QVariantList PointerPageController::availableShaderEffects() const
{
    QVariantList out;
    if (!m_registry) {
        return out;
    }
    const QList<PointerShaderEffect> effects = m_registry->availableEffects();
    out.reserve(effects.size());
    for (const PointerShaderEffect& effect : effects) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), effect.id);
        m.insert(QStringLiteral("name"), effect.name);
        m.insert(QStringLiteral("description"), effect.description);
        m.insert(QStringLiteral("author"), effect.author);
        m.insert(QStringLiteral("version"), effect.version);
        m.insert(QStringLiteral("category"), effect.category);
        m.insert(QStringLiteral("isUserEffect"), effect.isUserEffect);
        m.insert(QStringLiteral("previewPath"), effect.previewPath);
        m.insert(QStringLiteral("layer"), PointerShaderEffect::layerToken(effect.layer));
        m.insert(QStringLiteral("parameters"), parameterRows(effect));
        out.append(m);
    }
    return out;
}

QVariantList PointerPageController::shaderParameters(const QString& effectId) const
{
    if (!m_registry || effectId.isEmpty() || !m_registry->hasEffect(effectId)) {
        return {};
    }
    return parameterRows(m_registry->effect(effectId));
}

QStringList PointerPageController::chain() const
{
    QStringList out;
    if (!m_settings) {
        return out;
    }
    const PointerProfile profile = m_settings->pointerChain();
    out.reserve(profile.layers.size());
    for (const PointerLayer& layer : profile.layers) {
        // A layer whose pack is gone keeps its slot: ChainEditor renders it as
        // "(missing: <id>)", which is what makes it removable and what lets it
        // survive a reinstall untouched.
        out.append(layer.effectId);
    }
    return out;
}

QVariantMap PointerPageController::chainParams() const
{
    QVariantMap out;
    if (!m_settings) {
        return out;
    }
    const PointerProfile profile = m_settings->pointerChain();
    for (const PointerLayer& layer : profile.layers) {
        // STORED overrides only — the editor layers them over the pack's
        // declared defaults itself. Merging the defaults in here would pin the
        // layer to whatever they were when it was added.
        if (!layer.parameters.isEmpty()) {
            out.insert(layer.effectId, layer.parameters);
        }
    }
    return out;
}

QStringList PointerPageController::disabledPacks() const
{
    QStringList out;
    if (!m_settings) {
        return out;
    }
    const PointerProfile profile = m_settings->pointerChain();
    for (const PointerLayer& layer : profile.layers) {
        if (!layer.enabled) {
            out.append(layer.effectId);
        }
    }
    return out;
}

void PointerPageController::setChain(const QStringList& packIds)
{
    if (!m_settings) {
        return;
    }
    const PointerProfile current = m_settings->pointerChain();
    QHash<QString, PointerLayer> byId;
    byId.reserve(current.layers.size());
    for (const PointerLayer& layer : current.layers) {
        byId.insert(layer.effectId, layer);
    }

    PointerProfile profile;
    profile.layers.reserve(packIds.size());
    QSet<QString> seen;
    for (const QString& id : packIds) {
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);
        // A surviving id carries its stored parameters and its enabled flag
        // through the reorder; a new one starts with neither, so the pack's
        // declared defaults apply and a pack update carries the new ones.
        const auto it = byId.constFind(id);
        if (it != byId.constEnd()) {
            profile.layers.append(*it);
        } else {
            PointerLayer layer;
            layer.effectId = id;
            profile.layers.append(layer);
        }
    }
    m_settings->setPointerChain(profile);
}

void PointerPageController::setChainLayerEnabled(const QString& packId, bool enabled)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    const int index = indexOfPack(profile, packId);
    if (index < 0 || profile.layers[index].enabled == enabled) {
        return;
    }
    profile.layers[index].enabled = enabled;
    m_settings->setPointerChain(profile);
}

void PointerPageController::setChainParam(const QString& packId, const QString& paramId, const QVariant& value)
{
    if (!m_settings || paramId.isEmpty()) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    const int index = indexOfPack(profile, packId);
    if (index < 0 || profile.layers[index].parameters.value(paramId) == value) {
        return;
    }
    profile.layers[index].parameters.insert(paramId, value);
    m_settings->setPointerChain(profile);
}

void PointerPageController::setChainParams(const QString& packId, const QVariantMap& params)
{
    if (!m_settings || params.isEmpty()) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    const int index = indexOfPack(profile, packId);
    if (index < 0) {
        return;
    }
    QVariantMap& target = profile.layers[index].parameters;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
    m_settings->setPointerChain(profile);
}

} // namespace PlasmaZones

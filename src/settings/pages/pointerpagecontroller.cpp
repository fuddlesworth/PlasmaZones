// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pointerpagecontroller.h"

#include "pointerpreviewcontroller.h"

#include "core/interfaces/isettings.h"

#include <PhosphorPointer/PointerProfile.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>

#include <QLatin1String>

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

QVariantList PointerPageController::chain() const
{
    QVariantList out;
    if (!m_settings) {
        return out;
    }
    const PointerProfile profile = m_settings->pointerChain();
    out.reserve(profile.layers.size());
    for (const PointerLayer& layer : profile.layers) {
        QVariantMap m;
        m.insert(QStringLiteral("effectId"), layer.effectId);
        m.insert(QStringLiteral("enabled"), layer.enabled);
        const bool known = m_registry && m_registry->hasEffect(layer.effectId);
        // The pack's defaults under the user's overrides, so the editor renders
        // a live control for every declared parameter rather than a blank one
        // for anything untouched. A layer whose pack is gone keeps its stored
        // overrides verbatim, which is what lets it survive a reinstall.
        QVariantMap params;
        QString name = layer.effectId;
        if (known) {
            const PointerShaderEffect effect = m_registry->effect(layer.effectId);
            name = effect.name.isEmpty() ? layer.effectId : effect.name;
            params = effect.defaultParams();
        }
        for (auto it = layer.parameters.constBegin(); it != layer.parameters.constEnd(); ++it) {
            params.insert(it.key(), it.value());
        }
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("parameters"), params);
        m.insert(QStringLiteral("missing"), !known);
        out.append(m);
    }
    return out;
}

void PointerPageController::setChain(const QVariantList& layers)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile;
    profile.layers.reserve(layers.size());
    for (const QVariant& entry : layers) {
        const QVariantMap m = entry.toMap();
        PointerLayer layer;
        layer.effectId = m.value(QLatin1String("effectId")).toString();
        if (layer.effectId.isEmpty()) {
            continue;
        }
        // Absent reads as enabled, matching PointerProfile::fromJson.
        layer.enabled = m.contains(QLatin1String("enabled")) ? m.value(QLatin1String("enabled")).toBool() : true;
        layer.parameters = m.value(QLatin1String("parameters")).toMap();
        profile.layers.append(layer);
    }
    m_settings->setPointerChain(profile);
}

void PointerPageController::addLayer(const QString& effectId)
{
    if (!m_settings || effectId.isEmpty()) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    PointerLayer layer;
    layer.effectId = effectId;
    // No parameters stored: an empty override map means "the pack's declared
    // defaults", so a pack whose defaults change on update carries the new ones
    // instead of being pinned to whatever they were on the day it was added.
    profile.layers.append(layer);
    m_settings->setPointerChain(profile);
}

void PointerPageController::removeLayer(int index)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    if (index < 0 || index >= profile.layers.size()) {
        return;
    }
    profile.layers.removeAt(index);
    m_settings->setPointerChain(profile);
}

void PointerPageController::moveLayer(int from, int to)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    const int count = profile.layers.size();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return;
    }
    profile.layers.move(from, to);
    m_settings->setPointerChain(profile);
}

void PointerPageController::setLayerEnabled(int index, bool enabled)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    if (index < 0 || index >= profile.layers.size()) {
        return;
    }
    if (profile.layers[index].enabled == enabled) {
        return;
    }
    profile.layers[index].enabled = enabled;
    m_settings->setPointerChain(profile);
}

void PointerPageController::setLayerParam(int index, const QString& paramId, const QVariant& value)
{
    if (!m_settings || paramId.isEmpty()) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    if (index < 0 || index >= profile.layers.size()) {
        return;
    }
    if (profile.layers[index].parameters.value(paramId) == value) {
        return;
    }
    profile.layers[index].parameters.insert(paramId, value);
    m_settings->setPointerChain(profile);
}

void PointerPageController::setLayerParams(int index, const QVariantMap& params)
{
    if (!m_settings || params.isEmpty()) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    if (index < 0 || index >= profile.layers.size()) {
        return;
    }
    QVariantMap& target = profile.layers[index].parameters;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        target.insert(it.key(), it.value());
    }
    m_settings->setPointerChain(profile);
}

void PointerPageController::resetLayerParams(int index)
{
    if (!m_settings) {
        return;
    }
    PointerProfile profile = m_settings->pointerChain();
    if (index < 0 || index >= profile.layers.size()) {
        return;
    }
    if (profile.layers[index].parameters.isEmpty()) {
        return;
    }
    // Cleared rather than filled with the pack's defaults, so the layer tracks
    // the pack across an update the same way a freshly added one does.
    profile.layers[index].parameters.clear();
    m_settings->setPointerChain(profile);
}

} // namespace PlasmaZones

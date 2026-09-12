// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayshadertree.h"

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <QJsonValue>
#include <QLatin1String>

namespace PlasmaZones {

// ─── OverlayShaderProfile ───────────────────────────────────────────────────

QJsonObject OverlayShaderProfile::toJson() const
{
    QJsonObject obj;
    if (!shaderId.isEmpty())
        obj[QLatin1String(JsonFieldShaderId)] = shaderId;
    if (!parameters.isEmpty())
        obj[QLatin1String(JsonFieldParameters)] = QJsonObject::fromVariantMap(parameters);
    if (!presetId.isEmpty())
        obj[QLatin1String(JsonFieldPresetId)] = presetId;
    return obj;
}

OverlayShaderProfile OverlayShaderProfile::fromJson(const QJsonObject& obj)
{
    OverlayShaderProfile profile;
    profile.shaderId = obj.value(QLatin1String(JsonFieldShaderId)).toString();
    profile.parameters = obj.value(QLatin1String(JsonFieldParameters)).toObject().toVariantMap();
    profile.presetId = obj.value(QLatin1String(JsonFieldPresetId)).toString();
    return profile;
}

// ─── OverlayShaderTree ──────────────────────────────────────────────────────

OverlayShaderProfile OverlayShaderTree::resolve(const QString& layoutId) const
{
    // ONE step, not a walk-up: an override or the baseline, nothing between. That
    // is the difference from the other two trees, and it is why this one does not
    // share `resolve()` with them.
    if (m_store.hasOverride(layoutId))
        return m_store.directOverride(layoutId);
    return m_store.baseline();
}

OverlayShaderProfile OverlayShaderTree::directOverride(const QString& layoutId) const
{
    return m_store.directOverride(layoutId);
}

bool OverlayShaderTree::hasOverride(const QString& layoutId) const
{
    return m_store.hasOverride(layoutId);
}

QStringList OverlayShaderTree::overriddenLayouts() const
{
    // SORTED, not insertion order. PathKeyedOverrides tracks insertion order for
    // every consumer, and this one deliberately ignores it: the keys are layout
    // UUIDs with no meaningful sequence, the wire format is a key-addressed
    // object, and a stable sorted view is what a picker wants. Sorting here is
    // also what makes this tree's equality order-free below.
    QStringList ids = m_store.keys();
    ids.sort();
    return ids;
}

bool OverlayShaderTree::isEmpty() const
{
    return m_store.baseline().isEmpty() && m_store.hasNoOverrides();
}

void OverlayShaderTree::setOverride(const QString& layoutId, const OverlayShaderProfile& profile)
{
    // The empty-key refusal lives in PathKeyedOverrides now, where all three
    // trees' copies of it collapsed into one. The reason is unchanged and was
    // the same for each of them: "" is the BASELINE's path everywhere else in
    // these APIs, so an override keyed on it would be one `resolve()` could
    // never return.
    m_store.setOverride(layoutId, profile);
}

bool OverlayShaderTree::clearOverride(const QString& layoutId)
{
    return m_store.clearOverride(layoutId);
}

void OverlayShaderTree::setBaseline(const OverlayShaderProfile& profile)
{
    m_store.setBaseline(profile);
}

QJsonObject OverlayShaderTree::toJson() const
{
    QJsonObject obj;
    if (!m_store.baseline().isEmpty())
        obj[QLatin1String(JsonFieldBaseline)] = m_store.baseline().toJson();
    if (!m_store.hasNoOverrides()) {
        // The KEY-ADDRESSED form, unlike the other two trees' arrays. Walking in
        // insertion order changes nothing here, because QJsonObject is sorted by
        // key regardless — which is also why this tree's order is not part of its
        // identity and its equality below is order-free.
        QJsonObject overrides;
        m_store.forEachInOrder([&overrides](const QString& layoutId, const OverlayShaderProfile& profile) {
            overrides[layoutId] = profile.toJson();
        });
        obj[QLatin1String(JsonFieldOverrides)] = overrides;
    }
    return obj;
}

OverlayShaderTree OverlayShaderTree::fromJson(const QJsonObject& obj)
{
    OverlayShaderTree tree;
    tree.m_store.setBaseline(OverlayShaderProfile::fromJson(obj.value(QLatin1String(JsonFieldBaseline)).toObject()));
    const QJsonObject overrides = obj.value(QLatin1String(JsonFieldOverrides)).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject())
            continue;
        tree.setOverride(it.key(), OverlayShaderProfile::fromJson(it.value().toObject()));
    }
    return tree;
}

bool OverlayShaderTree::operator==(const OverlayShaderTree& other) const
{
    // Order-free by construction: every ordered view this tree exposes
    // (overriddenLayouts, toJson via QJsonObject) is sorted, so the insertion
    // order PathKeyedOverrides tracks is invisible here and must not be compared.
    // That is the disagreement between the three trees — the other two DO compare
    // it, because their overrides are an array on the wire — and it is exactly
    // why the shared container hands out `sameBaseline` / `sameOverrides` /
    // `sameKeyOrder` separately instead of an operator== that would pick one
    // policy for all three.
    return m_store.sameBaseline(other.m_store) && m_store.sameOverrides(other.m_store);
}

OverlayShaderProfile withPresetsResolved(const OverlayShaderProfile& profile,
                                         const PhosphorShaders::ShaderPresetRegistry& presets)
{
    if (profile.presetId.isEmpty()) {
        return profile;
    }
    OverlayShaderProfile out = profile;
    // The profile's own parameters are the DELTA set, so they are the last
    // argument: preset values first, this assignment's edits on top.
    out.parameters = presets.resolveParams(PhosphorShaders::ShaderFamily::Overlay, profile.shaderId, profile.presetId,
                                           profile.parameters);
    // Cleared so a second flatten is a no-op rather than a double application
    // the moment anything overlays two already-flattened profiles.
    out.presetId.clear();
    return out;
}

} // namespace PlasmaZones

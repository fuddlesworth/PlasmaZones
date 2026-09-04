// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayshadertree.h"

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
    return obj;
}

OverlayShaderProfile OverlayShaderProfile::fromJson(const QJsonObject& obj)
{
    OverlayShaderProfile profile;
    profile.shaderId = obj.value(QLatin1String(JsonFieldShaderId)).toString();
    profile.parameters = obj.value(QLatin1String(JsonFieldParameters)).toObject().toVariantMap();
    return profile;
}

// ─── OverlayShaderTree ──────────────────────────────────────────────────────

OverlayShaderProfile OverlayShaderTree::resolve(const QString& layoutId) const
{
    const auto it = m_overrides.constFind(layoutId);
    if (it != m_overrides.constEnd())
        return it.value();
    return m_baseline;
}

OverlayShaderProfile OverlayShaderTree::directOverride(const QString& layoutId) const
{
    return m_overrides.value(layoutId);
}

bool OverlayShaderTree::hasOverride(const QString& layoutId) const
{
    return m_overrides.contains(layoutId);
}

QStringList OverlayShaderTree::overriddenLayouts() const
{
    QStringList ids = m_overrides.keys();
    ids.sort();
    return ids;
}

bool OverlayShaderTree::isEmpty() const
{
    return m_baseline.isEmpty() && m_overrides.isEmpty();
}

void OverlayShaderTree::setOverride(const QString& layoutId, const OverlayShaderProfile& profile)
{
    if (layoutId.isEmpty())
        return;
    m_overrides.insert(layoutId, profile);
}

bool OverlayShaderTree::clearOverride(const QString& layoutId)
{
    return m_overrides.remove(layoutId) != 0;
}

void OverlayShaderTree::setBaseline(const OverlayShaderProfile& profile)
{
    m_baseline = profile;
}

QJsonObject OverlayShaderTree::toJson() const
{
    QJsonObject obj;
    if (!m_baseline.isEmpty())
        obj[QLatin1String(JsonFieldBaseline)] = m_baseline.toJson();
    if (!m_overrides.isEmpty()) {
        QJsonObject overrides;
        for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it)
            overrides[it.key()] = it.value().toJson();
        obj[QLatin1String(JsonFieldOverrides)] = overrides;
    }
    return obj;
}

OverlayShaderTree OverlayShaderTree::fromJson(const QJsonObject& obj)
{
    OverlayShaderTree tree;
    tree.m_baseline = OverlayShaderProfile::fromJson(obj.value(QLatin1String(JsonFieldBaseline)).toObject());
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
    // Order-free by construction: overrides live in a hash and every
    // ordered view (overriddenLayouts, toJson via QJsonObject) is sorted,
    // so equality has no order component to consider (unlike the animation
    // tree, whose operator== also compares order; see the order-insensitive
    // compare the decoration setter had to build around that).
    return m_baseline == other.m_baseline && m_overrides == other.m_overrides;
}

} // namespace PlasmaZones

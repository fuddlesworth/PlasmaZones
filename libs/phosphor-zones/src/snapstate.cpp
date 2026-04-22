// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/SnapState.h>

#include <QJsonArray>

namespace PhosphorZones {

SnapState::SnapState(const QString& screenId, QObject* parent)
    : QObject(parent)
    , m_screenId(screenId)
{
}

SnapState::~SnapState() = default;

// ── IPlacementState ─────────────────────────────────────────────────────────

QString SnapState::screenId() const
{
    return m_screenId;
}

int SnapState::windowCount() const
{
    QSet<QString> all;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        all.insert(it.key());
    }
    all.unite(m_floatingWindows);
    return all.size();
}

QStringList SnapState::managedWindows() const
{
    QSet<QString> all;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        all.insert(it.key());
    }
    all.unite(m_floatingWindows);
    QStringList list(all.begin(), all.end());
    list.sort();
    return list;
}

bool SnapState::containsWindow(const QString& windowId) const
{
    return m_windowZoneAssignments.contains(windowId) || m_floatingWindows.contains(windowId);
}

bool SnapState::isFloating(const QString& windowId) const
{
    return m_floatingWindows.contains(windowId);
}

QStringList SnapState::floatingWindows() const
{
    QStringList list(m_floatingWindows.begin(), m_floatingWindows.end());
    list.sort();
    return list;
}

QString SnapState::placementIdForWindow(const QString& windowId) const
{
    if (m_floatingWindows.contains(windowId)) {
        return {};
    }
    const auto it = m_windowZoneAssignments.constFind(windowId);
    if (it == m_windowZoneAssignments.constEnd() || it->isEmpty()) {
        return {};
    }
    return it->first();
}

QJsonObject SnapState::toJson() const
{
    QJsonObject obj;
    obj[QLatin1String("screenId")] = m_screenId;

    QJsonObject zones;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        QJsonArray arr;
        for (const QString& z : it.value()) {
            arr.append(z);
        }
        zones[it.key()] = arr;
    }
    obj[QLatin1String("zoneAssignments")] = zones;

    QJsonArray floating;
    for (const QString& w : m_floatingWindows) {
        floating.append(w);
    }
    obj[QLatin1String("floatingWindows")] = floating;

    QJsonObject preTile;
    for (auto it = m_preTileGeometries.constBegin(); it != m_preTileGeometries.constEnd(); ++it) {
        QJsonObject geo;
        geo[QLatin1String("x")] = it->geometry.x();
        geo[QLatin1String("y")] = it->geometry.y();
        geo[QLatin1String("w")] = it->geometry.width();
        geo[QLatin1String("h")] = it->geometry.height();
        if (!it->connectorName.isEmpty()) {
            geo[QLatin1String("connector")] = it->connectorName;
        }
        preTile[it.key()] = geo;
    }
    obj[QLatin1String("preTileGeometries")] = preTile;

    QJsonObject preFloat;
    for (auto it = m_preFloatZoneAssignments.constBegin(); it != m_preFloatZoneAssignments.constEnd(); ++it) {
        QJsonArray arr;
        for (const QString& z : it.value()) {
            arr.append(z);
        }
        preFloat[it.key()] = arr;
    }
    obj[QLatin1String("preFloatZones")] = preFloat;

    return obj;
}

// ── Zone Assignment CRUD ────────────────────────────────────────────────────

void SnapState::assignWindowToZone(const QString& windowId, const QString& zoneId)
{
    m_windowZoneAssignments[windowId] = {zoneId};
    Q_EMIT windowAssigned(windowId, zoneId);
    Q_EMIT stateChanged();
}

void SnapState::assignWindowToZones(const QString& windowId, const QStringList& zoneIds)
{
    m_windowZoneAssignments[windowId] = zoneIds;
    Q_EMIT windowAssigned(windowId, zoneIds.isEmpty() ? QString() : zoneIds.first());
    Q_EMIT stateChanged();
}

void SnapState::unassignWindow(const QString& windowId)
{
    if (m_windowZoneAssignments.remove(windowId)) {
        Q_EMIT windowUnassigned(windowId);
        Q_EMIT stateChanged();
    }
}

QString SnapState::zoneForWindow(const QString& windowId) const
{
    const auto it = m_windowZoneAssignments.constFind(windowId);
    if (it == m_windowZoneAssignments.constEnd() || it->isEmpty()) {
        return {};
    }
    return it->first();
}

QStringList SnapState::zonesForWindow(const QString& windowId) const
{
    return m_windowZoneAssignments.value(windowId);
}

QStringList SnapState::windowsInZone(const QString& zoneId) const
{
    QStringList result;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (it->contains(zoneId)) {
            result.append(it.key());
        }
    }
    return result;
}

QStringList SnapState::snappedWindows() const
{
    QStringList result;
    result.reserve(m_windowZoneAssignments.size());
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        result.append(it.key());
    }
    return result;
}

bool SnapState::isWindowSnapped(const QString& windowId) const
{
    return m_windowZoneAssignments.contains(windowId);
}

// ── Floating State ──────────────────────────────────────────────────────────

void SnapState::setFloating(const QString& windowId, bool floating)
{
    bool changed = false;
    if (floating) {
        if (!m_floatingWindows.contains(windowId)) {
            m_floatingWindows.insert(windowId);
            changed = true;
        }
    } else {
        changed = m_floatingWindows.remove(windowId);
    }
    if (changed) {
        Q_EMIT floatingChanged(windowId, floating);
        Q_EMIT stateChanged();
    }
}

void SnapState::unsnapForFloat(const QString& windowId)
{
    const auto zones = zonesForWindow(windowId);
    if (!zones.isEmpty()) {
        m_preFloatZoneAssignments[windowId] = zones;
    }
    unassignWindow(windowId);
}

QString SnapState::preFloatZone(const QString& windowId) const
{
    const auto zones = m_preFloatZoneAssignments.value(windowId);
    return zones.isEmpty() ? QString() : zones.first();
}

QStringList SnapState::preFloatZones(const QString& windowId) const
{
    return m_preFloatZoneAssignments.value(windowId);
}

void SnapState::clearPreFloatZone(const QString& windowId)
{
    m_preFloatZoneAssignments.remove(windowId);
}

// ── Pre-Tile Geometry ───────────────────────────────────────────────────────

void SnapState::storePreTileGeometry(const QString& windowId, const QRect& geometry, const QString& connectorName,
                                     bool overwrite)
{
    if (!overwrite && m_preTileGeometries.contains(windowId)) {
        return;
    }
    m_preTileGeometries[windowId] = {geometry, connectorName};
    Q_EMIT stateChanged();
}

std::optional<QRect> SnapState::preTileGeometry(const QString& windowId) const
{
    const auto it = m_preTileGeometries.constFind(windowId);
    if (it == m_preTileGeometries.constEnd()) {
        return std::nullopt;
    }
    return it->geometry;
}

bool SnapState::hasPreTileGeometry(const QString& windowId) const
{
    return m_preTileGeometries.contains(windowId);
}

void SnapState::clearPreTileGeometry(const QString& windowId)
{
    if (m_preTileGeometries.remove(windowId)) {
        Q_EMIT stateChanged();
    }
}

// ── Window Lifecycle ────────────────────────────────────────────────────────

void SnapState::windowClosed(const QString& windowId)
{
    bool removed = false;
    removed |= m_windowZoneAssignments.remove(windowId);
    removed |= m_floatingWindows.remove(windowId);
    removed |= m_preTileGeometries.remove(windowId);
    removed |= m_preFloatZoneAssignments.remove(windowId);
    if (removed) {
        Q_EMIT stateChanged();
    }
}

void SnapState::clear()
{
    m_windowZoneAssignments.clear();
    m_floatingWindows.clear();
    m_preTileGeometries.clear();
    m_preFloatZoneAssignments.clear();
    Q_EMIT stateChanged();
}

// ── Rotation ────────────────────────────────────────────────────────────────

QStringList SnapState::rotateAssignments(bool clockwise)
{
    if (m_windowZoneAssignments.size() < 2) {
        return {};
    }

    QMap<QString, QString> zoneToWindow;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (!it->isEmpty()) {
            zoneToWindow[it->first()] = it.key();
        }
    }

    if (zoneToWindow.size() < 2) {
        return {};
    }

    QList<QString> sortedZones = zoneToWindow.keys();
    QList<QString> windowOrder;
    for (const auto& z : sortedZones) {
        windowOrder.append(zoneToWindow[z]);
    }

    if (clockwise) {
        windowOrder.prepend(windowOrder.takeLast());
    } else {
        windowOrder.append(windowOrder.takeFirst());
    }

    for (int i = 0; i < sortedZones.size(); ++i) {
        m_windowZoneAssignments[windowOrder[i]] = {sortedZones[i]};
    }

    Q_EMIT stateChanged();
    return windowOrder;
}

// ── Deserialization ─────────────────────────────────────────────────────────

SnapState* SnapState::fromJson(const QJsonObject& json, QObject* parent)
{
    const QString screenId = json.value(QLatin1String("screenId")).toString();
    if (screenId.isEmpty()) {
        return nullptr;
    }

    auto* state = new SnapState(screenId, parent);

    const QJsonObject zones = json.value(QLatin1String("zoneAssignments")).toObject();
    for (auto it = zones.constBegin(); it != zones.constEnd(); ++it) {
        QStringList ids;
        const QJsonArray arr = it->toArray();
        for (const auto& v : arr) {
            ids.append(v.toString());
        }
        state->m_windowZoneAssignments[it.key()] = ids;
    }

    const QJsonArray floating = json.value(QLatin1String("floatingWindows")).toArray();
    for (const auto& v : floating) {
        state->m_floatingWindows.insert(v.toString());
    }

    const QJsonObject preTile = json.value(QLatin1String("preTileGeometries")).toObject();
    for (auto it = preTile.constBegin(); it != preTile.constEnd(); ++it) {
        const QJsonObject geo = it->toObject();
        PreTileGeometry ptg;
        ptg.geometry = QRect(geo.value(QLatin1String("x")).toInt(), geo.value(QLatin1String("y")).toInt(),
                             geo.value(QLatin1String("w")).toInt(), geo.value(QLatin1String("h")).toInt());
        ptg.connectorName = geo.value(QLatin1String("connector")).toString();
        state->m_preTileGeometries[it.key()] = ptg;
    }

    const QJsonObject preFloat = json.value(QLatin1String("preFloatZones")).toObject();
    for (auto it = preFloat.constBegin(); it != preFloat.constEnd(); ++it) {
        QStringList ids;
        const QJsonArray arr = it->toArray();
        for (const auto& v : arr) {
            ids.append(v.toString());
        }
        state->m_preFloatZoneAssignments[it.key()] = ids;
    }

    return state;
}

} // namespace PhosphorZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngineApi/PlacementEngineBase.h>

#include <QJsonObject>
#include <QLatin1String>

namespace PhosphorEngineApi {

PlacementEngineBase::PlacementEngineBase(QObject* parent)
    : QObject(parent)
{
}

PlacementEngineBase::~PlacementEngineBase() = default;

WindowState PlacementEngineBase::windowState(const QString& windowId) const
{
    return m_windowStates.value(windowId, WindowState::Unmanaged);
}

void PlacementEngineBase::claimWindow(const QString& windowId, const QRect& geometry, const QString& screenId,
                                      bool overwrite)
{
    if (windowId.isEmpty()) {
        return;
    }

    WindowState oldState = m_windowStates.value(windowId, WindowState::Unmanaged);

    storeUnmanagedGeometry(windowId, geometry, screenId, overwrite);

    m_windowStates[windowId] = WindowState::EngineOwned;
    onWindowClaimed(windowId);

    if (oldState != WindowState::EngineOwned) {
        Q_EMIT windowStateTransitioned(windowId, oldState, WindowState::EngineOwned);
    }
}

void PlacementEngineBase::storeUnmanagedGeometry(const QString& windowId, const QRect& geometry,
                                                 const QString& screenId, bool overwrite)
{
    if (windowId.isEmpty() || !geometry.isValid()) {
        return;
    }
    if (overwrite || !m_unmanagedGeometries.contains(windowId)) {
        m_unmanagedGeometries[windowId] = {geometry, screenId};
    }

    static constexpr int MaxEntries = 200;
    while (m_unmanagedGeometries.size() > MaxEntries) {
        bool evicted = false;
        for (auto it = m_unmanagedGeometries.begin(); it != m_unmanagedGeometries.end(); ++it) {
            if (it.key() != windowId) {
                m_windowStates.remove(it.key());
                m_unmanagedGeometries.erase(it);
                evicted = true;
                break;
            }
        }
        if (!evicted) {
            break;
        }
    }
}

void PlacementEngineBase::releaseWindow(const QString& windowId)
{
    if (windowId.isEmpty() || !m_windowStates.contains(windowId)) {
        return;
    }

    WindowState oldState = m_windowStates.value(windowId);

    auto entry = m_unmanagedGeometries.take(windowId);
    m_windowStates.remove(windowId);

    onWindowReleased(windowId);

    if (entry.geometry.isValid()) {
        Q_EMIT geometryRestoreRequested(windowId, entry.geometry, entry.screenId);
    }

    Q_EMIT windowStateTransitioned(windowId, oldState, WindowState::Unmanaged);
}

void PlacementEngineBase::floatWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    WindowState current = m_windowStates.value(windowId, WindowState::Unmanaged);
    if (current != WindowState::EngineOwned) {
        return;
    }

    m_windowStates[windowId] = WindowState::Floated;
    onWindowFloated(windowId);
    Q_EMIT windowStateTransitioned(windowId, WindowState::EngineOwned, WindowState::Floated);
}

void PlacementEngineBase::unfloatWindow(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }

    WindowState current = m_windowStates.value(windowId, WindowState::Unmanaged);
    if (current != WindowState::Floated) {
        return;
    }

    m_windowStates[windowId] = WindowState::EngineOwned;
    onWindowUnfloated(windowId);
    Q_EMIT windowStateTransitioned(windowId, WindowState::Floated, WindowState::EngineOwned);
}

QRect PlacementEngineBase::unmanagedGeometry(const QString& windowId) const
{
    return m_unmanagedGeometries.value(windowId).geometry;
}

QString PlacementEngineBase::unmanagedScreen(const QString& windowId) const
{
    return m_unmanagedGeometries.value(windowId).screenId;
}

bool PlacementEngineBase::hasUnmanagedGeometry(const QString& windowId) const
{
    return m_unmanagedGeometries.contains(windowId);
}

void PlacementEngineBase::clearUnmanagedGeometry(const QString& windowId)
{
    m_unmanagedGeometries.remove(windowId);
}

void PlacementEngineBase::forgetWindow(const QString& windowId)
{
    m_unmanagedGeometries.remove(windowId);
    m_windowStates.remove(windowId);
}

void PlacementEngineBase::setUnmanagedGeometries(const QHash<QString, UnmanagedEntry>& geos)
{
    m_unmanagedGeometries = geos;
    for (auto it = geos.constBegin(); it != geos.constEnd(); ++it) {
        if (!m_windowStates.contains(it.key())) {
            m_windowStates[it.key()] = WindowState::EngineOwned;
        }
    }
}

int PlacementEngineBase::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = 0;
    for (auto it = m_unmanagedGeometries.begin(); it != m_unmanagedGeometries.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            m_windowStates.remove(it.key());
            it = m_unmanagedGeometries.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    for (auto it = m_windowStates.begin(); it != m_windowStates.end();) {
        if (!aliveWindowIds.contains(it.key()) && !m_unmanagedGeometries.contains(it.key())) {
            it = m_windowStates.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    return pruned;
}

QJsonObject PlacementEngineBase::serializeBaseState() const
{
    QJsonObject obj;

    QJsonObject geos;
    for (auto it = m_unmanagedGeometries.constBegin(); it != m_unmanagedGeometries.constEnd(); ++it) {
        QJsonObject entry;
        entry[QLatin1String("x")] = it->geometry.x();
        entry[QLatin1String("y")] = it->geometry.y();
        entry[QLatin1String("w")] = it->geometry.width();
        entry[QLatin1String("h")] = it->geometry.height();
        if (!it->screenId.isEmpty()) {
            entry[QLatin1String("screen")] = it->screenId;
        }
        geos[it.key()] = entry;
    }
    obj[QLatin1String("unmanagedGeometries")] = geos;

    return obj;
}

void PlacementEngineBase::deserializeBaseState(const QJsonObject& state)
{
    m_unmanagedGeometries.clear();
    m_windowStates.clear();

    const QJsonObject geos = state.value(QLatin1String("unmanagedGeometries")).toObject();
    for (auto it = geos.constBegin(); it != geos.constEnd(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        const QJsonObject entry = it->toObject();
        const int w = entry.value(QLatin1String("w")).toInt();
        const int h = entry.value(QLatin1String("h")).toInt();
        if (w <= 0 || h <= 0) {
            continue;
        }
        UnmanagedEntry e;
        e.geometry = QRect(entry.value(QLatin1String("x")).toInt(), entry.value(QLatin1String("y")).toInt(), w, h);
        e.screenId = entry.value(QLatin1String("screen")).toString();
        m_unmanagedGeometries[it.key()] = e;
        m_windowStates[it.key()] = WindowState::EngineOwned;
    }
}

} // namespace PhosphorEngineApi

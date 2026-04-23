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
    return allManagedWindowIds().size();
}

QStringList SnapState::managedWindows() const
{
    QSet<QString> all = allManagedWindowIds();
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

    // preTileGeometries removed — PlacementEngineBase is the single store.

    QJsonObject preFloat;
    for (auto it = m_preFloatZoneAssignments.constBegin(); it != m_preFloatZoneAssignments.constEnd(); ++it) {
        QJsonArray arr;
        for (const QString& z : it.value()) {
            arr.append(z);
        }
        preFloat[it.key()] = arr;
    }
    obj[QLatin1String("preFloatZones")] = preFloat;

    QJsonObject screens;
    for (auto it = m_windowScreenAssignments.constBegin(); it != m_windowScreenAssignments.constEnd(); ++it) {
        screens[it.key()] = it.value();
    }
    obj[QLatin1String("screenAssignments")] = screens;

    QJsonObject desktops;
    for (auto it = m_windowDesktopAssignments.constBegin(); it != m_windowDesktopAssignments.constEnd(); ++it) {
        desktops[it.key()] = it.value();
    }
    obj[QLatin1String("desktopAssignments")] = desktops;

    QJsonObject preFloatScreens;
    for (auto it = m_preFloatScreenAssignments.constBegin(); it != m_preFloatScreenAssignments.constEnd(); ++it) {
        preFloatScreens[it.key()] = it.value();
    }
    obj[QLatin1String("preFloatScreens")] = preFloatScreens;

    QJsonObject lastUsed;
    lastUsed[QLatin1String("zoneId")] = m_lastUsedZoneId;
    lastUsed[QLatin1String("screenId")] = m_lastUsedScreenId;
    lastUsed[QLatin1String("class")] = m_lastUsedZoneClass;
    lastUsed[QLatin1String("desktop")] = m_lastUsedDesktop;
    obj[QLatin1String("lastUsedZone")] = lastUsed;

    QStringList sortedUserSnapped(m_userSnappedClasses.begin(), m_userSnappedClasses.end());
    sortedUserSnapped.sort();
    QJsonArray userSnapped;
    for (const QString& c : sortedUserSnapped) {
        userSnapped.append(c);
    }
    obj[QLatin1String("userSnappedClasses")] = userSnapped;

    return obj;
}

// ── Zone Assignment CRUD ────────────────────────────────────────────────────

void SnapState::assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                                   int virtualDesktop)
{
    if (zoneId.isEmpty()) {
        unassignWindow(windowId);
        return;
    }
    assignWindowToZones(windowId, {zoneId}, screenId, virtualDesktop);
}

void SnapState::assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                    int virtualDesktop)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (zoneIds.isEmpty()) {
        unassignWindow(windowId);
        return;
    }
    QStringList validZoneIds;
    validZoneIds.reserve(zoneIds.size());
    for (const auto& id : zoneIds) {
        if (!id.isEmpty()) {
            validZoneIds.append(id);
        }
    }
    if (validZoneIds.isEmpty()) {
        return;
    }

    QStringList previousZones = m_windowZoneAssignments.value(windowId);
    bool zoneChanged = (previousZones != validZoneIds);
    bool screenChanged = (m_windowScreenAssignments.value(windowId) != screenId);
    bool desktopChanged = (m_windowDesktopAssignments.value(windowId, -1) != virtualDesktop);
    bool wasFloating = m_floatingWindows.remove(windowId);

    m_windowZoneAssignments[windowId] = validZoneIds;
    m_windowScreenAssignments[windowId] = screenId;
    m_windowDesktopAssignments[windowId] = virtualDesktop;

    if (zoneChanged) {
        Q_EMIT windowAssigned(windowId, validZoneIds.first());
    }
    if (zoneChanged || screenChanged || desktopChanged || wasFloating) {
        Q_EMIT stateChanged();
    }
}

SnapState::UnassignResult SnapState::unassignWindow(const QString& windowId)
{
    UnassignResult result;
    QStringList previousZones = m_windowZoneAssignments.value(windowId);
    if (!m_windowZoneAssignments.remove(windowId)) {
        return result;
    }
    result.wasAssigned = true;
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);
    if (!m_lastUsedZoneId.isEmpty() && previousZones.contains(m_lastUsedZoneId)) {
        m_lastUsedZoneId.clear();
        m_lastUsedScreenId.clear();
        m_lastUsedZoneClass.clear();
        m_lastUsedDesktop = 0;
        result.lastUsedZoneCleared = true;
    }
    Q_EMIT windowUnassigned(windowId);
    Q_EMIT stateChanged();
    return result;
}

QString SnapState::screenForWindow(const QString& windowId) const
{
    return m_windowScreenAssignments.value(windowId);
}

int SnapState::desktopForWindow(const QString& windowId) const
{
    return m_windowDesktopAssignments.value(windowId, 0);
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

SnapState::UnassignResult SnapState::unsnapForFloat(const QString& windowId)
{
    const auto zones = zonesForWindow(windowId);
    if (!zones.isEmpty()) {
        m_preFloatZoneAssignments[windowId] = zones;
        const QString screen = screenForWindow(windowId);
        if (!screen.isEmpty()) {
            m_preFloatScreenAssignments[windowId] = screen;
        }
    }
    return unassignWindow(windowId);
}

QString SnapState::preFloatScreen(const QString& windowId) const
{
    return m_preFloatScreenAssignments.value(windowId);
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
    m_preFloatScreenAssignments.remove(windowId);
}

// Pre-tile geometry removed — PlacementEngineBase is the single store.

// ── Window Lifecycle ────────────────────────────────────────────────────────

bool SnapState::removeWindowData(const QString& windowId)
{
    bool removed = false;
    removed |= m_windowZoneAssignments.remove(windowId);
    removed |= m_windowScreenAssignments.remove(windowId);
    removed |= m_windowDesktopAssignments.remove(windowId);
    removed |= m_floatingWindows.remove(windowId);
    removed |= m_preFloatZoneAssignments.remove(windowId);
    removed |= m_preFloatScreenAssignments.remove(windowId);
    removed |= m_autoSnappedWindows.remove(windowId);
    return removed;
}

void SnapState::windowClosed(const QString& windowId)
{
    if (removeWindowData(windowId)) {
        Q_EMIT stateChanged();
    }
}

bool SnapState::isEmpty() const
{
    return m_windowZoneAssignments.isEmpty() && m_windowScreenAssignments.isEmpty()
        && m_windowDesktopAssignments.isEmpty() && m_floatingWindows.isEmpty() && m_preFloatZoneAssignments.isEmpty()
        && m_preFloatScreenAssignments.isEmpty() && m_lastUsedZoneId.isEmpty() && m_lastUsedScreenId.isEmpty()
        && m_lastUsedZoneClass.isEmpty() && m_lastUsedDesktop == 0 && m_userSnappedClasses.isEmpty()
        && m_autoSnappedWindows.isEmpty();
}

void SnapState::clear()
{
    if (isEmpty()) {
        return;
    }
    m_windowZoneAssignments.clear();
    m_windowScreenAssignments.clear();
    m_windowDesktopAssignments.clear();
    m_floatingWindows.clear();
    m_preFloatZoneAssignments.clear();
    m_preFloatScreenAssignments.clear();
    m_lastUsedZoneId.clear();
    m_lastUsedScreenId.clear();
    m_lastUsedZoneClass.clear();
    m_lastUsedDesktop = 0;
    m_userSnappedClasses.clear();
    m_autoSnappedWindows.clear();
    Q_EMIT stateChanged();
}

// ── Rotation ────────────────────────────────────────────────────────────────

QStringList SnapState::rotateAssignments(bool clockwise)
{
    if (m_windowZoneAssignments.size() < 2) {
        return {};
    }

    // Collect the full zone list for each window, keyed by primary (first) zone
    // for sort order. Multi-zone assignments are preserved through rotation.
    QMap<QString, QPair<QString, QStringList>> zoneToWindowAndZones;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (!it->isEmpty()) {
            zoneToWindowAndZones[it->first()] = {it.key(), it.value()};
        }
    }

    if (zoneToWindowAndZones.size() < 2) {
        return {};
    }

    QList<QString> sortedPrimaryZones = zoneToWindowAndZones.keys();
    QList<QPair<QString, QStringList>> windowsWithZones;
    for (const auto& z : sortedPrimaryZones) {
        windowsWithZones.append(zoneToWindowAndZones[z]);
    }

    if (clockwise) {
        windowsWithZones.prepend(windowsWithZones.takeLast());
    } else {
        windowsWithZones.append(windowsWithZones.takeFirst());
    }

    QStringList affected;
    for (int i = 0; i < sortedPrimaryZones.size(); ++i) {
        const QString& windowId = windowsWithZones[i].first;
        const QStringList& originalZones = windowsWithZones[i].second;
        if (originalZones.size() == 1) {
            m_windowZoneAssignments[windowId] = {sortedPrimaryZones[i]};
        } else {
            // Multi-zone: shift by the same offset as the primary zone moved.
            // Zones not in sortedPrimaryZones (i.e., spanned zones that no other
            // window uses as a primary) pass through unchanged — partial rotation
            // is acceptable since such zones have no rotation target.
            int srcIdx = sortedPrimaryZones.indexOf(originalZones.first());
            int delta = i - srcIdx;
            QStringList rotated;
            for (const QString& z : originalZones) {
                int zIdx = sortedPrimaryZones.indexOf(z);
                if (zIdx >= 0) {
                    int newIdx = (zIdx + delta + sortedPrimaryZones.size()) % sortedPrimaryZones.size();
                    rotated.append(sortedPrimaryZones[newIdx]);
                } else {
                    rotated.append(z);
                }
            }
            m_windowZoneAssignments[windowId] = rotated;
        }
        affected.append(windowId);
    }

    Q_EMIT stateChanged();
    return affected;
}

// ── Last-Used Zone Tracking ─────────────────────────────────────────────────

void SnapState::updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                                   int virtualDesktop)
{
    if (m_lastUsedZoneId == zoneId && m_lastUsedScreenId == screenId && m_lastUsedZoneClass == windowClass
        && m_lastUsedDesktop == virtualDesktop) {
        return;
    }
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenId = screenId;
    m_lastUsedZoneClass = windowClass;
    m_lastUsedDesktop = virtualDesktop;
    Q_EMIT stateChanged();
}

void SnapState::restoreLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass,
                                    int desktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenId = screenId;
    m_lastUsedZoneClass = zoneClass;
    m_lastUsedDesktop = desktop;
}

// ── Auto-Snap Bookkeeping ──────────────────────────────────────────────────

void SnapState::recordSnapIntent(const QString& windowClass, bool wasUserInitiated)
{
    if (wasUserInitiated && !windowClass.isEmpty() && !m_userSnappedClasses.contains(windowClass)) {
        m_userSnappedClasses.insert(windowClass);
        Q_EMIT stateChanged();
    }
}

void SnapState::markAsAutoSnapped(const QString& windowId)
{
    if (m_autoSnappedWindows.contains(windowId)) {
        return;
    }
    m_autoSnappedWindows.insert(windowId);
    Q_EMIT stateChanged();
}

bool SnapState::isAutoSnapped(const QString& windowId) const
{
    return m_autoSnappedWindows.contains(windowId);
}

bool SnapState::clearAutoSnapped(const QString& windowId)
{
    if (m_autoSnappedWindows.remove(windowId)) {
        Q_EMIT stateChanged();
        return true;
    }
    return false;
}

// ── Occupied Zone Queries ──────────────────────────────────────────────────

QSet<QString> SnapState::buildOccupiedZoneSet(const QString& screenFilter, int desktopFilter) const
{
    QSet<QString> occupied;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        if (!screenFilter.isEmpty()) {
            const QString windowScreen = m_windowScreenAssignments.value(it.key());
            if (windowScreen != screenFilter) {
                continue;
            }
        }
        if (desktopFilter > 0) {
            int windowDesktop = m_windowDesktopAssignments.value(it.key(), 0);
            if (windowDesktop != 0 && windowDesktop != desktopFilter) {
                continue;
            }
        }
        for (const QString& zoneId : it.value()) {
            occupied.insert(zoneId);
        }
    }
    return occupied;
}

int SnapState::pruneStaleAssignments(const QSet<QString>& aliveWindowIds)
{
    QSet<QString> allTracked;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        allTracked.insert(it.key());
    }
    for (auto it = m_windowScreenAssignments.constBegin(); it != m_windowScreenAssignments.constEnd(); ++it) {
        allTracked.insert(it.key());
    }
    for (auto it = m_windowDesktopAssignments.constBegin(); it != m_windowDesktopAssignments.constEnd(); ++it) {
        allTracked.insert(it.key());
    }
    allTracked.unite(m_floatingWindows);
    for (auto it = m_preFloatZoneAssignments.constBegin(); it != m_preFloatZoneAssignments.constEnd(); ++it) {
        allTracked.insert(it.key());
    }
    for (auto it = m_preFloatScreenAssignments.constBegin(); it != m_preFloatScreenAssignments.constEnd(); ++it) {
        allTracked.insert(it.key());
    }
    allTracked.unite(m_autoSnappedWindows);

    int pruned = 0;
    for (const QString& windowId : allTracked) {
        if (!aliveWindowIds.contains(windowId)) {
            removeWindowData(windowId);
            ++pruned;
        }
    }
    if (pruned > 0) {
        Q_EMIT stateChanged();
    }
    return pruned;
}

// ── Deserialization ─────────────────────────────────────────────────────────

SnapState* SnapState::fromJson(const QJsonObject& json, QObject* parent)
{
    const QString screenId = json.value(QLatin1String("screenId")).toString();
    auto* state = new SnapState(screenId, parent);

    const QJsonObject zones = json.value(QLatin1String("zoneAssignments")).toObject();
    for (auto it = zones.constBegin(); it != zones.constEnd(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        QStringList ids;
        const QJsonArray arr = it->toArray();
        for (const auto& v : arr) {
            const QString zoneId = v.toString();
            if (!zoneId.isEmpty()) {
                ids.append(zoneId);
            }
        }
        if (!ids.isEmpty()) {
            state->m_windowZoneAssignments[it.key()] = ids;
        }
    }

    const QJsonArray floating = json.value(QLatin1String("floatingWindows")).toArray();
    for (const auto& v : floating) {
        const QString wId = v.toString();
        if (!wId.isEmpty()) {
            state->m_floatingWindows.insert(wId);
        }
    }

    // preTileGeometries deserialization removed — PlacementEngineBase is the single store.

    const QJsonObject preFloat = json.value(QLatin1String("preFloatZones")).toObject();
    for (auto it = preFloat.constBegin(); it != preFloat.constEnd(); ++it) {
        if (it.key().isEmpty()) {
            continue;
        }
        QStringList ids;
        const QJsonArray arr = it->toArray();
        for (const auto& v : arr) {
            const QString zoneId = v.toString();
            if (!zoneId.isEmpty()) {
                ids.append(zoneId);
            }
        }
        if (!ids.isEmpty()) {
            state->m_preFloatZoneAssignments[it.key()] = ids;
        }
    }

    const QJsonObject screens = json.value(QLatin1String("screenAssignments")).toObject();
    for (auto it = screens.constBegin(); it != screens.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            state->m_windowScreenAssignments[it.key()] = it->toString();
        }
    }

    const QJsonObject desktops = json.value(QLatin1String("desktopAssignments")).toObject();
    for (auto it = desktops.constBegin(); it != desktops.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            state->m_windowDesktopAssignments[it.key()] = it->toInt();
        }
    }

    const QJsonObject preFloatScreens = json.value(QLatin1String("preFloatScreens")).toObject();
    for (auto it = preFloatScreens.constBegin(); it != preFloatScreens.constEnd(); ++it) {
        if (!it.key().isEmpty()) {
            state->m_preFloatScreenAssignments[it.key()] = it->toString();
        }
    }

    const QJsonObject lastUsed = json.value(QLatin1String("lastUsedZone")).toObject();
    state->m_lastUsedZoneId = lastUsed.value(QLatin1String("zoneId")).toString();
    state->m_lastUsedScreenId = lastUsed.value(QLatin1String("screenId")).toString();
    state->m_lastUsedZoneClass = lastUsed.value(QLatin1String("class")).toString();
    state->m_lastUsedDesktop = lastUsed.value(QLatin1String("desktop")).toInt();

    const QJsonArray userSnapped = json.value(QLatin1String("userSnappedClasses")).toArray();
    for (const auto& v : userSnapped) {
        const QString c = v.toString();
        if (!c.isEmpty()) {
            state->m_userSnappedClasses.insert(c);
        }
    }

    return state;
}

} // namespace PhosphorZones

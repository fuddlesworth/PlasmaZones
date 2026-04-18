// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Virtual screen configuration, geometry, and lookup methods.
// Part of ScreenManager — split for SRP.

#include "PhosphorScreens/Manager.h"

#include "PhosphorScreens/ScreenIdentity.h"
#include "PhosphorScreens/VirtualScreen.h"
#include "screenslogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QGuiApplication>
#include <QScreen>
#include <QSet>

#include <algorithm>
#include <limits>

namespace Phosphor::Screens {

namespace {
/// Minimum usable dimension (pixels) for a virtual screen available area.
/// If panel intersection leaves less than this, fall back to full virtual
/// geometry.
constexpr int MinUsableScreenDimension = 100;

/// Squared edge distance from a point to a rectangle (0 if inside).
/// qint64 to avoid overflow when squaring large pixel distances.
/// Exclusive-right semantics match VirtualScreenDef::absoluteGeometry()
/// — avoids dist=0 ties at boundary pixels between adjacent VSs.
qint64 edgeDistance(const QRect& rect, const QPoint& point)
{
    const qint64 dx = qMax(
        qint64(0),
        qMax(qint64(rect.left()) - qint64(point.x()), qint64(point.x()) - (qint64(rect.x()) + qint64(rect.width()))));
    const qint64 dy = qMax(
        qint64(0),
        qMax(qint64(rect.top()) - qint64(point.y()), qint64(point.y()) - (qint64(rect.y()) + qint64(rect.height()))));
    return dx * dx + dy * dy;
}

/// Exclusive-right containment: a point at x+width or y+height belongs
/// to the next virtual screen, not this one.
bool containsExclusive(const QRect& r, const QPoint& p)
{
    return p.x() >= r.x() && p.x() < r.x() + r.width() && p.y() >= r.y() && p.y() < r.y() + r.height();
}
} // namespace

bool ScreenManager::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: empty physicalScreenId";
        return false;
    }

    if (config.isEmpty()) {
        if (!m_virtualConfigs.contains(physicalScreenId)) {
            return true;
        }
        m_virtualConfigs.remove(physicalScreenId);
        m_effectiveScreenIdsDirty = true;
        invalidateVirtualGeometryCache(physicalScreenId);
        Q_EMIT virtualScreensChanged(physicalScreenId);
        return true;
    }

    QString error;
    if (!VirtualScreenConfig::isValid(config, physicalScreenId, m_cfg.maxVirtualScreensPerPhysical, &error)) {
        qCWarning(lcPhosphorScreens) << "setVirtualScreenConfig: rejected invalid config —" << error;
        return false;
    }

    const VirtualScreenConfig oldConfig = m_virtualConfigs.value(physicalScreenId);
    // approxEqual (not operator==) so a JSON-roundtripped config that
    // picked up tiny float deltas on load compares equal to the in-memory
    // source we wrote. Exact operator== here would re-emit the change
    // chain every reload. This is a change-detection skip-gate; the
    // tolerance is never observed by downstream state.
    if (oldConfig.approxEqual(config)) {
        return true;
    }

    // Detect "regions-only" changes — same VS IDs, just different rects.
    bool regionsOnly = false;
    if (oldConfig.screens.size() == config.screens.size() && oldConfig.screens.size() >= 2) {
        regionsOnly = true;
        QHash<QString, const VirtualScreenDef*> oldById;
        oldById.reserve(oldConfig.screens.size());
        for (const auto& def : oldConfig.screens) {
            oldById.insert(def.id, &def);
        }
        for (const auto& newDef : config.screens) {
            auto it = oldById.constFind(newDef.id);
            if (it == oldById.constEnd()) {
                regionsOnly = false;
                break;
            }
            const VirtualScreenDef* oldDef = it.value();
            if (oldDef->displayName != newDef.displayName || oldDef->index != newDef.index
                || oldDef->physicalScreenId != newDef.physicalScreenId) {
                regionsOnly = false;
                break;
            }
        }
    }

    m_virtualConfigs.insert(physicalScreenId, config);
    m_effectiveScreenIdsDirty = true;
    invalidateVirtualGeometryCache(physicalScreenId);
    if (regionsOnly) {
        Q_EMIT virtualScreenRegionsChanged(physicalScreenId);
    } else {
        Q_EMIT virtualScreensChanged(physicalScreenId);
    }
    return true;
}

VirtualScreenConfig ScreenManager::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualConfigs.value(physicalScreenId);
}

void ScreenManager::refreshVirtualConfigs(const QHash<QString, VirtualScreenConfig>& configs)
{
    QStringList toRemove;
    QStringList toApply;
    for (auto it = m_virtualConfigs.constBegin(); it != m_virtualConfigs.constEnd(); ++it) {
        if (!configs.contains(it.key())) {
            toRemove.append(it.key());
        }
    }
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        toApply.append(it.key());
    }
    std::sort(toRemove.begin(), toRemove.end());
    std::sort(toApply.begin(), toApply.end());

    for (const QString& physId : std::as_const(toRemove)) {
        VirtualScreenConfig empty;
        empty.physicalScreenId = physId;
        setVirtualScreenConfig(physId, empty);
    }
    for (const QString& physId : std::as_const(toApply)) {
        setVirtualScreenConfig(physId, configs.value(physId));
    }
}

QStringList ScreenManager::effectiveScreenIds() const
{
    if (!m_effectiveScreenIdsDirty) {
        return m_cachedEffectiveScreenIds;
    }

    QStringList result;
    for (auto* screen : m_trackedScreens) {
        QString physId = ScreenIdentity::identifierFor(screen);
        if (!ScreenIdentity::findByIdOrName(physId)) {
            // Identifier round-trip fails — QScreen* exists in our tracked
            // list but its identifier doesn't resolve back to any
            // currently-connected screen. Usually a hotplug race (screen
            // removal in flight). Log so identity-resolution drift doesn't
            // mask itself silently.
            qCDebug(lcPhosphorScreens) << "effectiveScreenIds: skipping tracked screen"
                                       << (screen ? screen->name() : QString()) << "— identifier" << physId
                                       << "does not resolve back to a live QScreen";
            continue;
        }
        auto it = m_virtualConfigs.constFind(physId);
        if (it != m_virtualConfigs.constEnd() && it->hasSubdivisions()) {
            for (const auto& vs : it->screens) {
                result.append(vs.id);
            }
        } else {
            result.append(physId);
        }
    }

    m_cachedEffectiveScreenIds = result;
    m_effectiveScreenIdsDirty = false;
    return result;
}

QStringList ScreenManager::virtualScreenIdsFor(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it != m_virtualConfigs.constEnd() && it->hasSubdivisions()) {
        QStringList ids;
        for (const auto& vs : it->screens) {
            ids.append(vs.id);
        }
        return ids;
    }
    return {physicalScreenId};
}

QRect ScreenManager::screenGeometry(const QString& screenId) const
{
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        if (!m_virtualGeometryCache.contains(screenId)) {
            QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
            rebuildVirtualGeometryCache(physId);
        }
        QRect cached = m_virtualGeometryCache.value(screenId);
        if (cached.isValid()) {
            return cached;
        }
        qCWarning(lcPhosphorScreens) << "screenGeometry: virtual screen" << screenId
                                     << "not found in cache, returning invalid geometry";
        return QRect();
    }
    QScreen* screen = ScreenIdentity::findByIdOrName(screenId);
    return screen ? screen->geometry() : QRect();
}

QRect ScreenManager::screenAvailableGeometry(const QString& screenId) const
{
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        QRect vsGeom = screenGeometry(screenId);
        if (!vsGeom.isValid()) {
            return QRect();
        }
        QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        QScreen* screen = ScreenIdentity::findByIdOrName(physId);
        if (!screen) {
            return vsGeom;
        }
        QRect physAvail = actualAvailableGeometry(screen);
        QRect result = vsGeom.intersected(physAvail);
        if (!result.isValid() || result.width() < MinUsableScreenDimension
            || result.height() < MinUsableScreenDimension) {
            qCWarning(lcPhosphorScreens) << "screenAvailableGeometry: panel leaves insufficient space in virtual screen"
                                         << screenId << "- intersection:" << result << "- using full virtual geometry";
            return vsGeom;
        }
        return result;
    }

    QScreen* screen = ScreenIdentity::findByIdOrName(screenId);
    return screen ? actualAvailableGeometry(screen) : QRect();
}

QScreen* ScreenManager::physicalQScreenFor(const QString& screenId) const
{
    QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    return ScreenIdentity::findByIdOrName(physId);
}

bool ScreenManager::hasVirtualScreens(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    return it != m_virtualConfigs.constEnd() && it->hasSubdivisions();
}

VirtualScreenDef::PhysicalEdges ScreenManager::physicalEdgesFor(const QString& screenId) const
{
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return {true, true, true, true};
    }
    QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    auto it = m_virtualConfigs.constFind(physId);
    if (it == m_virtualConfigs.constEnd()) {
        return {true, true, true, true};
    }
    for (const auto& vs : it->screens) {
        if (vs.id == screenId) {
            return vs.physicalEdges();
        }
    }
    return {true, true, true, true};
}

QString ScreenManager::virtualScreenAt(const QPoint& globalPos, const QString& physicalScreenId) const
{
    QScreen* screen = ScreenIdentity::findByIdOrName(physicalScreenId);
    return virtualScreenAtWithScreen(globalPos, physicalScreenId, screen);
}

QString ScreenManager::virtualScreenAtWithScreen(const QPoint& globalPos, const QString& physicalScreenId,
                                                 QScreen* screen) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it == m_virtualConfigs.constEnd() || !screen) {
        return {};
    }
    QRect physGeom = screen->geometry();
    for (const auto& vs : it->screens) {
        QRect absGeom = vs.absoluteGeometry(physGeom);
        if (containsExclusive(absGeom, globalPos)) {
            return vs.id;
        }
    }
    QString nearestId;
    qint64 minDist = std::numeric_limits<qint64>::max();
    for (const auto& vs : it->screens) {
        QRect absGeom = vs.absoluteGeometry(physGeom);
        qint64 dist = edgeDistance(absGeom, globalPos);
        if (dist < minDist) {
            minDist = dist;
            nearestId = vs.id;
        }
    }
    return nearestId;
}

QString ScreenManager::effectiveScreenAt(const QPoint& globalPos) const
{
    for (auto* screen : m_trackedScreens) {
        // Exclusive-right containment to match VS lookup semantics — a
        // point on the boundary between two adjacent physical screens
        // belongs to the screen whose origin is that pixel, never both.
        // QRect::contains() is inclusive on all four edges, which would
        // pick the first screen in iteration order and hide layout bugs
        // where two screens share an edge.
        const QRect g = screen->geometry();
        if (!(globalPos.x() >= g.x() && globalPos.x() < g.x() + g.width() && globalPos.y() >= g.y()
              && globalPos.y() < g.y() + g.height())) {
            continue;
        }
        QString physId = ScreenIdentity::identifierFor(screen);
        if (hasVirtualScreens(physId)) {
            QString vsId = virtualScreenAtWithScreen(globalPos, physId, screen);
            if (!vsId.isEmpty()) {
                return vsId;
            }
        }
        return physId;
    }
    return {};
}

void ScreenManager::invalidateVirtualGeometryCache(const QString& physicalScreenId) const
{
    if (physicalScreenId.isEmpty()) {
        m_virtualGeometryCache.clear();
        return;
    }
    auto it = m_virtualGeometryCache.begin();
    while (it != m_virtualGeometryCache.end()) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) == physicalScreenId) {
            it = m_virtualGeometryCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ScreenManager::rebuildVirtualGeometryCache(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    if (it == m_virtualConfigs.constEnd()) {
        return;
    }
    QScreen* screen = ScreenIdentity::findByIdOrName(physicalScreenId);
    if (!screen) {
        return;
    }
    invalidateVirtualGeometryCache(physicalScreenId);
    QRect physGeom = screen->geometry();
    for (const auto& vs : it->screens) {
        m_virtualGeometryCache.insert(vs.id, vs.absoluteGeometry(physGeom));
    }
}

} // namespace Phosphor::Screens

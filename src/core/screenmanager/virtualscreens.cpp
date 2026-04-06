// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Virtual screen configuration, geometry, and lookup methods.
// Part of ScreenManager — split from screenmanager.cpp for SRP.

#include "../screenmanager.h"
#include "../virtualscreen.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QSet>
#include <limits>

namespace PlasmaZones {

namespace {
/// Minimum usable dimension (pixels) for a virtual screen available area.
/// If panel intersection leaves less than this, fall back to full virtual geometry.
constexpr int MinUsableScreenDimension = 100;

/// Squared edge distance from a point to a rectangle (0 if inside).
/// Uses qint64 to avoid overflow when squaring large pixel distances.
/// Uses exclusive-right semantics (x + width, y + height) to match
/// VirtualScreenDef::absoluteGeometry() which constructs QRects from
/// (left, top, width, height). This avoids dist=0 ties at boundary
/// pixels between adjacent virtual screens.
qint64 edgeDistance(const QRect& rect, const QPoint& point)
{
    const qint64 dx =
        qMax(qint64(0), qMax(qint64(rect.left() - point.x()), qint64(point.x() - (rect.x() + rect.width()))));
    const qint64 dy =
        qMax(qint64(0), qMax(qint64(rect.top() - point.y()), qint64(point.y() - (rect.y() + rect.height()))));
    return dx * dx + dy * dy;
}

/// Exclusive-right containment check: a point at x+width or y+height
/// belongs to the next virtual screen, not this one. QRect::contains()
/// is inclusive-right and would create boundary ambiguity.
bool containsExclusive(const QRect& r, const QPoint& p)
{
    return p.x() >= r.x() && p.x() < r.x() + r.width() && p.y() >= r.y() && p.y() < r.y() + r.height();
}
} // anonymous namespace

bool ScreenManager::setVirtualScreenConfig(const QString& physicalScreenId, const VirtualScreenConfig& config)
{
    if (physicalScreenId.isEmpty()) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: empty physicalScreenId";
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

    // Validate: physicalScreenId must match config
    if (config.physicalScreenId != physicalScreenId) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: config physicalScreenId" << config.physicalScreenId
                            << "does not match parameter" << physicalScreenId;
        return false;
    }

    // Validate: need at least 2 screens for a meaningful subdivision
    if (config.screens.size() < 2) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: need at least 2 screens for subdivision, got"
                            << config.screens.size();
        return false;
    }

    // Validate: all defs must pass isValid() and have consistent IDs
    for (const auto& def : config.screens) {
        if (!def.isValid()) {
            qCWarning(lcScreen) << "setVirtualScreenConfig: invalid VirtualScreenDef" << def.id
                                << "region:" << def.region;
            return false;
        }
        if (def.physicalScreenId != physicalScreenId) {
            qCWarning(lcScreen) << "setVirtualScreenConfig: def.physicalScreenId" << def.physicalScreenId
                                << "does not match physicalScreenId" << physicalScreenId << "for def" << def.id;
            return false;
        }
        QString expectedId = VirtualScreenId::make(physicalScreenId, def.index);
        if (def.id != expectedId) {
            qCWarning(lcScreen) << "setVirtualScreenConfig: def.id" << def.id << "does not match expected" << expectedId
                                << "for index" << def.index;
            return false;
        }
    }

    // Validate: all def.id values are unique
    {
        QSet<QString> seenIds;
        for (const auto& def : config.screens) {
            if (seenIds.contains(def.id)) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: duplicate def.id" << def.id;
                return false;
            }
            seenIds.insert(def.id);
        }
    }

    // Validate: all def.index values are unique
    {
        QSet<int> seenIndices;
        for (const auto& def : config.screens) {
            if (seenIndices.contains(def.index)) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: duplicate def.index" << def.index;
                return false;
            }
            seenIndices.insert(def.index);
        }
    }

    // Validate: no two regions overlap (pairwise intersection check, tolerance-aware)
    for (int i = 0; i < config.screens.size(); ++i) {
        for (int j = i + 1; j < config.screens.size(); ++j) {
            QRectF intersection = config.screens[i].region.intersected(config.screens[j].region);
            if (intersection.width() > VirtualScreenDef::Tolerance
                && intersection.height() > VirtualScreenDef::Tolerance) {
                qCWarning(lcScreen) << "setVirtualScreenConfig: overlapping regions between" << config.screens[i].id
                                    << "and" << config.screens[j].id;
                return false;
            }
        }
    }

    // Validate: regions should approximately cover [0,1]x[0,1]
    qreal totalArea = 0.0;
    for (const auto& def : config.screens) {
        totalArea += def.region.width() * def.region.height();
    }
    if (totalArea < 0.99) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: insufficient coverage for" << physicalScreenId << "- total area"
                            << totalArea << "< 0.99, rejecting config";
        return false;
    }
    if (totalArea > 1.01) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: excessive coverage for" << physicalScreenId << "- total area"
                            << totalArea << "> 1.01, rejecting config";
        return false;
    }

    if (m_virtualConfigs.value(physicalScreenId) == config) {
        return true;
    }

    m_virtualConfigs.insert(physicalScreenId, config);
    m_effectiveScreenIdsDirty = true;
    invalidateVirtualGeometryCache(physicalScreenId);
    Q_EMIT virtualScreensChanged(physicalScreenId);
    return true;
}

VirtualScreenConfig ScreenManager::virtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualConfigs.value(physicalScreenId);
}

QStringList ScreenManager::effectiveScreenIds() const
{
    if (!m_effectiveScreenIdsDirty) {
        return m_cachedEffectiveScreenIds;
    }

    QStringList result;

    for (auto* screen : m_trackedScreens) {
        QString physId = Utils::screenIdentifier(screen);
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
    if (VirtualScreenId::isVirtual(screenId)) {
        if (!m_virtualGeometryCache.contains(screenId)) {
            QString physId = VirtualScreenId::extractPhysicalId(screenId);
            rebuildVirtualGeometryCache(physId);
        }
        QRect cached = m_virtualGeometryCache.value(screenId);
        if (cached.isValid()) {
            return cached;
        }
        // Virtual screen ID is not in the cache — the VS config was removed or
        // never existed. Return invalid QRect rather than falling through to
        // physical screen lookup, which would strip "/vs:N" and return the full
        // physical geometry. Callers do not expect a virtual screen ID to
        // silently resolve to a different (larger) geometry.
        qCWarning(lcScreen) << "screenGeometry: virtual screen" << screenId
                            << "not found in cache, returning invalid geometry";
        return QRect();
    }

    // Physical screen — try tracked screens first, then fallback
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    return screen ? screen->geometry() : QRect();
}

QRect ScreenManager::screenAvailableGeometry(const QString& screenId) const
{
    if (VirtualScreenId::isVirtual(screenId)) {
        QRect vsGeom = screenGeometry(screenId);
        if (!vsGeom.isValid()) {
            return QRect();
        }

        // Get the physical screen's available geometry and intersect.
        // NOTE: This duplicates the physical screen lookup already done inside
        // screenGeometry(). Acceptable cost since QScreen* lookup is cheap
        // (linear scan of a small list) and extracting it from screenGeometry
        // would require an invasive signature change or internal overload.
        QString physId = VirtualScreenId::extractPhysicalId(screenId);
        QScreen* screen = Utils::findScreenByIdOrName(physId);
        if (!screen) {
            return vsGeom;
        }

        QRect physAvail = actualAvailableGeometry(screen);
        QRect result = vsGeom.intersected(physAvail);
        // If panel consumes most of the virtual screen, fall back to full virtual geometry
        // to avoid zero/unusable available areas
        if (!result.isValid() || result.width() < MinUsableScreenDimension
            || result.height() < MinUsableScreenDimension) {
            qCWarning(lcScreen) << "screenAvailableGeometry: panel leaves insufficient space in virtual screen"
                                << screenId << "- intersection:" << result << "- using full virtual geometry";
            return vsGeom;
        }
        return result;
    }

    // Physical screen
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    return screen ? actualAvailableGeometry(screen) : QRect();
}

QScreen* ScreenManager::physicalQScreenFor(const QString& screenId) const
{
    QString physId = VirtualScreenId::extractPhysicalId(screenId);

    return Utils::findScreenByIdOrName(physId);
}

bool ScreenManager::hasVirtualScreens(const QString& physicalScreenId) const
{
    auto it = m_virtualConfigs.constFind(physicalScreenId);
    return it != m_virtualConfigs.constEnd() && it->hasSubdivisions();
}

/// Convenience alias for virtualScreenIdsFor().
/// Both exist because effectiveIdsForPhysical reads more naturally at call
/// sites that treat virtual and physical screens interchangeably, while
/// virtualScreenIdsFor is clearer in virtual-screen-specific code paths.
QStringList ScreenManager::effectiveIdsForPhysical(const QString& physicalScreenId) const
{
    return virtualScreenIdsFor(physicalScreenId);
}

QStringList ScreenManager::effectiveScreenIdsWithFallback()
{
    auto* mgr = instance();
    if (mgr) {
        QStringList ids = mgr->effectiveScreenIds();
        if (!ids.isEmpty()) {
            return ids;
        }
    }
    QStringList result;
    for (const auto* screen : Utils::allScreens()) {
        result.append(Utils::screenIdentifier(screen));
    }
    return result;
}

VirtualScreenDef::PhysicalEdges ScreenManager::physicalEdgesFor(const QString& screenId) const
{
    // Physical screens: all edges are at the physical boundary
    if (!VirtualScreenId::isVirtual(screenId)) {
        return {true, true, true, true};
    }

    QString physId = VirtualScreenId::extractPhysicalId(screenId);
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
    QScreen* screen = Utils::findScreenByIdOrName(physicalScreenId);
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

    // Point falls in gap between virtual screens — find nearest by edge distance
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
        // Use inclusive containment for physical screens (matches Qt's QRect semantics).
        // Virtual screen sub-containment uses containsExclusive to avoid boundary ambiguity.
        if (!screen->geometry().contains(globalPos)) {
            continue;
        }

        QString physId = Utils::screenIdentifier(screen);
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

    // Remove all cached entries belonging to this physical screen.
    // Use extractPhysicalId() for exact matching instead of prefix matching,
    // which would false-match if one physical screen ID is a prefix of another.
    auto it = m_virtualGeometryCache.begin();
    while (it != m_virtualGeometryCache.end()) {
        if (VirtualScreenId::extractPhysicalId(it.key()) == physicalScreenId) {
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

    QScreen* screen = Utils::findScreenByIdOrName(physicalScreenId);
    if (!screen) {
        return;
    }

    // Clear stale entries for this physical screen before inserting new ones.
    // Without this, renamed or removed virtual screen defs would leave ghost
    // entries in the cache after a config change.
    invalidateVirtualGeometryCache(physicalScreenId);

    QRect physGeom = screen->geometry();
    for (const auto& vs : it->screens) {
        m_virtualGeometryCache.insert(vs.id, vs.absoluteGeometry(physGeom));
    }
}

} // namespace PlasmaZones

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
#include <algorithm>
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
    const qint64 dx = qMax(
        qint64(0),
        qMax(qint64(rect.left()) - qint64(point.x()), qint64(point.x()) - (qint64(rect.x()) + qint64(rect.width()))));
    const qint64 dy = qMax(
        qint64(0),
        qMax(qint64(rect.top()) - qint64(point.y()), qint64(point.y()) - (qint64(rect.y()) + qint64(rect.height()))));
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

    QString error;
    // ScreenManager doesn't enforce a max-screens cap here — that's a policy
    // decision owned by Settings (the source of truth) and ScreenAdaptor (the
    // D-Bus boundary). ScreenManager just enforces structural/geometric
    // invariants so its cache can't hold internally inconsistent state.
    if (!VirtualScreenConfig::isValid(config, physicalScreenId, /*maxScreensPerPhysical=*/0, &error)) {
        qCWarning(lcScreen) << "setVirtualScreenConfig: rejected invalid config —" << error;
        return false;
    }

    const VirtualScreenConfig oldConfig = m_virtualConfigs.value(physicalScreenId);
    if (oldConfig == config) {
        return true;
    }

    // Detect "regions-only" changes where the VS ID set is unchanged — same
    // ids, same count, same indices, same display names. This lets downstream
    // handlers take a lightweight path that only updates geometry without
    // re-running mode resolution, orphan cleanup, window migration, etc.
    bool regionsOnly = false;
    if (oldConfig.screens.size() == config.screens.size() && oldConfig.screens.size() >= 2) {
        regionsOnly = true;
        QSet<QString> oldIds;
        for (const auto& def : oldConfig.screens) {
            oldIds.insert(def.id);
        }
        for (const auto& def : config.screens) {
            if (!oldIds.contains(def.id)) {
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
    // Compute the delta in two ordered passes so the downstream signal fan-out
    // is deterministic across runs:
    //   1. Removals first (entries in cache but absent from `configs`),
    //   2. Then additions/changes (entries in `configs`).
    // Both passes iterate sorted physical IDs so observers see a stable order
    // when multiple physical screens change in one refresh. Without sorting,
    // QHash iteration order is unspecified and downstream cross-screen state
    // (window migrations, autotile updates) can race subtly between runs.
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

    // Pass 1: tear down removed entries. setVirtualScreenConfig with an empty
    // config short-circuits if there's nothing to remove, so this is idempotent.
    for (const QString& physId : std::as_const(toRemove)) {
        VirtualScreenConfig empty;
        empty.physicalScreenId = physId;
        setVirtualScreenConfig(physId, empty);
    }

    // Pass 2: apply additions and updates. setVirtualScreenConfig early-returns
    // when the new config is bit-identical to the current one, so unchanged
    // entries are no-ops and don't fire the virtualScreensChanged signal.
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
        QString physId = Utils::screenIdentifier(screen);
        // Skip configs whose physical screen can no longer be resolved — they are
        // stale entries left over from a screen removal that has not yet been pruned.
        if (!Utils::findScreenByIdOrName(physId)) {
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

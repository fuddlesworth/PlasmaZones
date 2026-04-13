// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "virtualscreenswapper.h"

#include "../config/settings.h"
#include "logging.h"
#include "shared/virtualscreenid.h"
#include "spatialadjacency.h"
#include "virtualscreen.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace PlasmaZones {

VirtualScreenSwapper::VirtualScreenSwapper(Settings* settings)
    : m_settings(settings)
{
    Q_ASSERT(settings);
}

bool VirtualScreenSwapper::swapInDirection(const QString& currentVirtualScreenId, const QString& direction)
{
    if (!VirtualScreenId::isVirtual(currentVirtualScreenId)) {
        qCDebug(lcCore) << "VirtualScreenSwapper::swapInDirection: current id is not virtual:"
                        << currentVirtualScreenId;
        return false;
    }

    const QString physId = VirtualScreenId::extractPhysicalId(currentVirtualScreenId);
    VirtualScreenConfig cfg = m_settings->virtualScreenConfig(physId);
    if (cfg.screens.size() < 2) {
        return false;
    }

    int currentIndex = -1;
    QList<QRectF> regions;
    regions.reserve(cfg.screens.size());
    for (int i = 0; i < cfg.screens.size(); ++i) {
        regions.append(cfg.screens[i].region);
        if (cfg.screens[i].id == currentVirtualScreenId) {
            currentIndex = i;
        }
    }
    if (currentIndex < 0) {
        qCDebug(lcCore) << "VirtualScreenSwapper::swapInDirection: current VS not in config:" << currentVirtualScreenId;
        return false;
    }

    const int targetIndex = SpatialAdjacency::findAdjacentRect(regions[currentIndex], regions, direction);
    if (targetIndex < 0 || targetIndex == currentIndex) {
        qCDebug(lcCore) << "VirtualScreenSwapper::swapInDirection: no adjacent VS in direction" << direction;
        return false;
    }

    const QString targetId = cfg.screens[targetIndex].id;
    if (!cfg.swapRegions(currentVirtualScreenId, targetId)) {
        return false;
    }

    m_settings->setVirtualScreenConfig(physId, cfg);
    return true;
}

bool VirtualScreenSwapper::rotate(const QString& physicalScreenId, bool clockwise)
{
    if (physicalScreenId.isEmpty() || VirtualScreenId::isVirtual(physicalScreenId)) {
        qCDebug(lcCore) << "VirtualScreenSwapper::rotate: invalid physicalScreenId:" << physicalScreenId;
        return false;
    }

    VirtualScreenConfig cfg = m_settings->virtualScreenConfig(physicalScreenId);
    if (cfg.screens.size() < 2) {
        return false;
    }

    // Spatial clockwise ring order: sort by angle from the config's centroid,
    // measured CW from "up" (atan2(dx, -dy)). Angles are normalised to
    // [0, 2π) so left of centroid (negative atan2 result) wraps past bottom.
    QPointF centroid(0.0, 0.0);
    for (const auto& def : cfg.screens) {
        centroid += def.region.center();
    }
    centroid /= static_cast<qreal>(cfg.screens.size());

    auto cwAngle = [centroid](const QRectF& r) {
        const QPointF p = r.center() - centroid;
        qreal a = std::atan2(p.x(), -p.y());
        if (a < 0) {
            a += 2.0 * M_PI;
        }
        return a;
    };

    QVector<int> order;
    order.reserve(cfg.screens.size());
    for (int i = 0; i < cfg.screens.size(); ++i) {
        order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&cfg, &cwAngle](int a, int b) {
        return cwAngle(cfg.screens[a].region) < cwAngle(cfg.screens[b].region);
    });

    QVector<QString> orderedIds;
    orderedIds.reserve(order.size());
    for (int idx : order) {
        orderedIds.append(cfg.screens[idx].id);
    }

    if (!cfg.rotateRegions(orderedIds, clockwise)) {
        return false;
    }

    m_settings->setVirtualScreenConfig(physicalScreenId, cfg);
    return true;
}

} // namespace PlasmaZones

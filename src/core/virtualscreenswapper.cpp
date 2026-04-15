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
#include <QStringLiteral>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace PlasmaZones {

namespace {

// Match VirtualScreenDef::Tolerance so collinearity detection stays
// consistent with the rest of the codebase's float comparison policy.
constexpr qreal kCollinearEpsilon = VirtualScreenDef::Tolerance;

/// Build a clockwise ring order for the given virtual screen defs.
///
/// Default path: sort by angle from the config centroid measured CW from
/// "up" (atan2(dx, -dy)) and normalised to [0, 2π). This matches user
/// expectation for 2D grids.
///
/// Degenerate path: when every centre shares a y-coordinate (horizontal
/// strip) or every centre shares an x-coordinate (vertical strip), the
/// atan2 ring collapses on signed-zero behaviour and produces an order
/// that is not visually clockwise. Fall back to a sort along the varying
/// axis — left→right for horizontal strips, top→bottom for vertical
/// strips — which on a 1D layout is the closest analogue to "clockwise"
/// rotation users actually expect.
QVector<int> computeCwRingOrder(const QVector<VirtualScreenDef>& screens)
{
    QVector<int> order;
    order.reserve(screens.size());
    for (int i = 0; i < screens.size(); ++i) {
        order.append(i);
    }

    // Detect 1D collinear layouts before computing the centroid.
    bool collinearY = true;
    bool collinearX = true;
    const qreal y0 = screens.first().region.center().y();
    const qreal x0 = screens.first().region.center().x();
    for (const auto& def : screens) {
        const QPointF c = def.region.center();
        if (std::abs(c.y() - y0) > kCollinearEpsilon) {
            collinearY = false;
        }
        if (std::abs(c.x() - x0) > kCollinearEpsilon) {
            collinearX = false;
        }
    }

    if (collinearY) {
        // Horizontal strip — sort by x ascending (left to right).
        std::stable_sort(order.begin(), order.end(), [&screens](int a, int b) {
            return screens[a].region.center().x() < screens[b].region.center().x();
        });
        return order;
    }
    if (collinearX) {
        // Vertical strip — sort by y ascending (top to bottom).
        std::stable_sort(order.begin(), order.end(), [&screens](int a, int b) {
            return screens[a].region.center().y() < screens[b].region.center().y();
        });
        return order;
    }

    // 2D layout: angle-from-centroid sort, CW from "up".
    QPointF centroid(0.0, 0.0);
    for (const auto& def : screens) {
        centroid += def.region.center();
    }
    centroid /= static_cast<qreal>(screens.size());

    auto cwAngle = [centroid](const QRectF& r) {
        const QPointF p = r.center() - centroid;
        qreal a = std::atan2(p.x(), -p.y());
        if (a < 0) {
            a += 2.0 * M_PI;
        }
        return a;
    };

    std::stable_sort(order.begin(), order.end(), [&screens, &cwAngle](int a, int b) {
        return cwAngle(screens[a].region) < cwAngle(screens[b].region);
    });
    return order;
}

} // namespace

VirtualScreenSwapper::VirtualScreenSwapper(Settings* settings)
    : m_settings(settings)
{
    Q_ASSERT(settings);
}

VirtualScreenSwapper::Result VirtualScreenSwapper::swapInDirection(const QString& currentVirtualScreenId,
                                                                   const QString& direction)
{
    if (!VirtualScreenId::isVirtual(currentVirtualScreenId)) {
        qCDebug(lcCore) << "VirtualScreenSwapper::swapInDirection: current id is not virtual:"
                        << currentVirtualScreenId;
        return Result::NotVirtual;
    }
    if (direction.isEmpty()) {
        return Result::InvalidDirection;
    }

    const QString physId = VirtualScreenId::extractPhysicalId(currentVirtualScreenId);
    VirtualScreenConfig cfg = m_settings->virtualScreenConfig(physId);
    if (cfg.screens.size() < 2) {
        return Result::NoSubdivision;
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
        return Result::UnknownVirtualScreen;
    }

    // findAdjacentRect already skips same-centre candidates, so a returned
    // index can never equal currentIndex — only < 0 means "no sibling".
    const int targetIndex = SpatialAdjacency::findAdjacentRect(regions[currentIndex], regions, direction);
    if (targetIndex < 0) {
        qCDebug(lcCore) << "VirtualScreenSwapper::swapInDirection: no adjacent VS in direction" << direction;
        return Result::NoSiblingInDirection;
    }

    const QString targetId = cfg.screens[targetIndex].id;
    if (!cfg.swapRegions(currentVirtualScreenId, targetId)) {
        return Result::SettingsRejected;
    }

    if (!m_settings->setVirtualScreenConfig(physId, cfg)) {
        return Result::SettingsRejected;
    }
    return Result::Ok;
}

VirtualScreenSwapper::Result VirtualScreenSwapper::rotate(const QString& physicalScreenId, bool clockwise)
{
    if (physicalScreenId.isEmpty() || VirtualScreenId::isVirtual(physicalScreenId)) {
        qCDebug(lcCore) << "VirtualScreenSwapper::rotate: invalid physicalScreenId:" << physicalScreenId;
        return Result::NotVirtual;
    }

    VirtualScreenConfig cfg = m_settings->virtualScreenConfig(physicalScreenId);
    if (cfg.screens.size() < 2) {
        return Result::NoSubdivision;
    }

    const QVector<int> order = computeCwRingOrder(cfg.screens);

    QVector<QString> orderedIds;
    orderedIds.reserve(order.size());
    for (int idx : order) {
        orderedIds.append(cfg.screens[idx].id);
    }

    if (!cfg.rotateRegions(orderedIds, clockwise)) {
        return Result::SettingsRejected;
    }

    if (!m_settings->setVirtualScreenConfig(physicalScreenId, cfg)) {
        return Result::SettingsRejected;
    }
    return Result::Ok;
}

QString VirtualScreenSwapper::reasonString(Result result)
{
    switch (result) {
    case Result::Ok:
        return QString();
    case Result::NotVirtual:
        return QStringLiteral("not_virtual");
    case Result::NoSubdivision:
        return QStringLiteral("no_subdivision");
    case Result::UnknownVirtualScreen:
        return QStringLiteral("unknown_vs");
    case Result::NoSiblingInDirection:
        return QStringLiteral("no_sibling");
    case Result::InvalidDirection:
        return QStringLiteral("invalid_direction");
    case Result::SettingsRejected:
        return QStringLiteral("settings_rejected");
    }
    return QString();
}

} // namespace PlasmaZones

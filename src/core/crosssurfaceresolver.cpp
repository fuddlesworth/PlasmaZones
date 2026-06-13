// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crosssurfaceresolver.h"

#include <PhosphorGeometry/DirectionalNeighbor.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QList>
#include <QRectF>

namespace PlasmaZones {

CrossSurfaceResolver::CrossSurfaceResolver(PhosphorScreens::ScreenManager* screenManager,
                                           PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager)
    : m_screenManager(screenManager)
    , m_virtualDesktopManager(virtualDesktopManager)
{
}

QString CrossSurfaceResolver::neighborOutputInDirection(const QString& screenId, const QString& direction) const
{
    const auto dir = PhosphorGeometry::directionFromString(direction);
    if (!dir.has_value() || !m_screenManager) {
        return QString();
    }
    const QRect sourceGeom = m_screenManager->screenGeometry(screenId);
    if (!sourceGeom.isValid()) {
        return QString();
    }

    // Candidate rects for every OTHER connected output, with a parallel id map.
    QList<QRectF> candidates;
    QStringList candidateIds;
    const QStringList ids = m_screenManager->effectiveScreenIds();
    for (const QString& id : ids) {
        if (id == screenId) {
            continue;
        }
        const QRect geom = m_screenManager->screenGeometry(id);
        if (geom.isValid()) {
            candidates.append(QRectF(geom));
            candidateIds.append(id);
        }
    }

    const int pick = PhosphorGeometry::directionalNeighbor(QRectF(sourceGeom), candidates, *dir);
    return pick < 0 ? QString() : candidateIds.at(pick);
}

int CrossSurfaceResolver::neighborDesktopInDirection(int currentDesktop, const QString& direction) const
{
    const auto dir = PhosphorGeometry::directionFromString(direction);
    if (!dir.has_value() || !m_virtualDesktopManager) {
        return 0;
    }
    return PhosphorGeometry::neighborDesktopInDirection(currentDesktop, m_virtualDesktopManager->desktopCount(),
                                                        m_virtualDesktopManager->desktopRows(), *dir);
}

} // namespace PlasmaZones

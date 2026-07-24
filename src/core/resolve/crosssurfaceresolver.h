// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorEngine/ICrossSurfaceResolver.h>

#include <QString>

namespace PhosphorScreens {
class ScreenManager;
}
namespace PhosphorWorkspaces {
class VirtualDesktopManager;
}

namespace PlasmaZones {

/**
 * @brief Daemon-side ICrossSurfaceResolver over the screen-topology and
 *        virtual-desktop services.
 *
 * Neighbour outputs are resolved geometrically (PhosphorGeometry::directionalNeighbor
 * over connected-output rects); neighbour desktops via the desktop-grid
 * arithmetic (PhosphorGeometry::neighborDesktopInDirection over the manager's
 * count + rows). One instance is injected into both placement engines so the
 * geometry/desktop knowledge lives in exactly one place. Borrows both services;
 * they must outlive the resolver.
 */
class PLASMAZONES_EXPORT CrossSurfaceResolver : public PhosphorEngine::ICrossSurfaceResolver
{
public:
    CrossSurfaceResolver(PhosphorScreens::ScreenManager* screenManager,
                         PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager);

    QString neighborOutputInDirection(const QString& screenId, const QString& direction) const override;
    int neighborDesktopInDirection(int currentDesktop, const QString& direction) const override;

private:
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager = nullptr;
};

} // namespace PlasmaZones

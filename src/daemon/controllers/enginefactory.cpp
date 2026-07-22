// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "enginefactory.h"

// Concrete engine includes — only this TU needs them.
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/resolve/crosssurfaceresolver.h"
#include "core/interfaces/isettings.h"
#include "core/resolve/screenmoderouter.h"

namespace PlasmaZones {

EngineSet createEngines(PhosphorZones::LayoutRegistry* layoutManager,
                        PhosphorPlacement::WindowTrackingService* windowTracker,
                        PhosphorScreens::ScreenManager* screenManager,
                        PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                        PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings,
                        PhosphorWorkspaces::VirtualDesktopManager* vdm, PhosphorEngine::WindowRegistry* windowRegistry)
{
    // --- AutotileEngine ---
    auto autotile = std::make_unique<PhosphorTileEngine::AutotileEngine>(layoutManager, windowTracker, screenManager,
                                                                         algorithmRegistry, nullptr);
    autotile->setWindowRegistry(windowRegistry);
    autotile->setEngineSettings(settings);

    // --- SnapEngine ---
    auto snap =
        std::make_unique<PhosphorSnapEngine::SnapEngine>(layoutManager, windowTracker, zoneDetector, vdm, nullptr);
    snap->setEngineSettings(settings);

    // Cross-wire: SnapEngine needs a reference to AutotileEngine for
    // isActiveOnScreen routing.
    snap->setAutotileEngine(autotile.get());

    // --- CrossSurfaceResolver ---
    // One resolver, shared by both engines, resolves neighbour outputs
    // (geometrically) and neighbour desktops (grid arithmetic) when directional
    // navigation reaches a layout boundary.
    auto crossSurfaceResolver = std::make_unique<CrossSurfaceResolver>(screenManager, vdm);
    autotile->setCrossSurfaceResolver(crossSurfaceResolver.get());
    snap->setCrossSurfaceResolver(crossSurfaceResolver.get());

    // --- ScreenModeRouter ---
    auto router = std::make_unique<ScreenModeRouter>(layoutManager, snap.get(), autotile.get());

    return EngineSet{.crossSurfaceResolver = std::move(crossSurfaceResolver),
                     .autotile = std::move(autotile),
                     .snap = std::move(snap),
                     .router = std::move(router)};
}

} // namespace PlasmaZones

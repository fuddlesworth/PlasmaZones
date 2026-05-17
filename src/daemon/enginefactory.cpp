// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "enginefactory.h"

// Concrete engine includes — only this TU needs them.
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include "../core/screenmoderouter.h"

namespace PlasmaZones {

EngineSet createEngines(PhosphorZones::LayoutRegistry* layoutManager,
                        PhosphorPlacement::WindowTrackingService* windowTracker,
                        Phosphor::Screens::ScreenManager* screenManager,
                        PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                        PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings,
                        PhosphorWorkspaces::VirtualDesktopManager* vdm, PhosphorEngine::WindowRegistry* windowRegistry,
                        QObject* parent)
{
    Q_UNUSED(parent)
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

    // --- ScrollEngine ---
    // ScrollEngine is geometry-agnostic and receives window events via the
    // router, so — unlike autotile/snap — it needs no ScreenManager,
    // WindowTrackingService, algorithm registry or zone detector.
    auto scroll = std::make_unique<PhosphorScrollEngine::ScrollEngine>(nullptr);
    scroll->setEngineSettings(settings);

    // --- ScreenModeRouter ---
    auto router = std::make_unique<ScreenModeRouter>(layoutManager, snap.get(), autotile.get(), scroll.get());

    return EngineSet{std::move(autotile), std::move(snap), std::move(scroll), std::move(router)};
}

} // namespace PlasmaZones

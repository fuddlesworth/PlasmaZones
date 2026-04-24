// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "enginefactory.h"

// Concrete engine includes — only this TU needs them.
#include "../autotile/AutotileEngine.h"
#include "../snap/SnapEngine.h"
#include "../core/screenmoderouter.h"

namespace PlasmaZones {

EngineSet createEngines(PhosphorZones::LayoutRegistry* layoutManager, WindowTrackingService* windowTracker,
                        Phosphor::Screens::ScreenManager* screenManager,
                        PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                        PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings, VirtualDesktopManager* vdm,
                        WindowRegistry* windowRegistry, QObject* parent)
{
    // --- AutotileEngine ---
    auto autotile =
        std::make_unique<AutotileEngine>(layoutManager, windowTracker, screenManager, algorithmRegistry, parent);
    autotile->setWindowRegistry(windowRegistry);

    // --- SnapEngine ---
    auto snap = std::make_unique<SnapEngine>(layoutManager, windowTracker, zoneDetector, settings, vdm, parent);

    // Cross-wire: SnapEngine needs a reference to AutotileEngine for
    // isActiveOnScreen routing.
    snap->setAutotileEngine(autotile.get());

    // --- ScreenModeRouter ---
    auto router = std::make_unique<ScreenModeRouter>(layoutManager, snap.get(), autotile.get());

    return EngineSet{std::move(autotile), std::move(snap), std::move(router)};
}

} // namespace PlasmaZones

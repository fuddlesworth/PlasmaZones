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
    EngineSet set;

    // --- AutotileEngine ---
    set.autotile =
        std::make_unique<AutotileEngine>(layoutManager, windowTracker, screenManager, algorithmRegistry, parent);
    set.autotile->setWindowRegistry(windowRegistry);

    // --- SnapEngine ---
    set.snap = std::make_unique<SnapEngine>(layoutManager, windowTracker, zoneDetector, settings, vdm, parent);

    // Cross-wire: SnapEngine needs a reference to AutotileEngine for
    // isActiveOnScreen routing.
    set.snap->setAutotileEngine(set.autotile.get());

    // --- ScreenModeRouter ---
    set.router = std::make_unique<ScreenModeRouter>(layoutManager, set.snap.get(), set.autotile.get());

    return set;
}

} // namespace PlasmaZones

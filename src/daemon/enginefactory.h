// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class QObject;

namespace PhosphorZones {
class LayoutRegistry;
class IZoneDetector;
} // namespace PhosphorZones

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
}

namespace PhosphorTileEngine {
class AutotileEngine;
}

namespace PhosphorSnapEngine {
class SnapEngine;
}

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PhosphorEngine {
class WindowRegistry;
}

namespace PhosphorPlacement {
class WindowTrackingService;
}

namespace PhosphorWorkspaces {
class VirtualDesktopManager;
}

namespace PlasmaZones {

class ISettings;
class ScreenModeRouter;

struct EngineSet
{
    std::unique_ptr<PhosphorTileEngine::AutotileEngine> autotile;
    std::unique_ptr<PhosphorSnapEngine::SnapEngine> snap;
    std::unique_ptr<PhosphorScrollEngine::ScrollEngine> scroll;
    // Declared last so it is destroyed first: the router holds raw pointers
    // into the three engines above and must not outlive them.
    std::unique_ptr<ScreenModeRouter> router;
};

/**
 * @brief Create the three built-in placement engines (snap, autotile, scroll) and the mode router.
 *
 * Concrete engine headers are included in the .cpp — the factory header
 * only forward-declares them. The caller (Daemon) must wire persistence
 * delegates, signal connections, and adaptor setup after receiving the
 * returned EngineSet.
 *
 * The "BuiltIn" qualifier signals that this is the interim factory shape:
 * the eventual plugin architecture (see project_plugin_architecture memory)
 * will replace this with a registry-iterating factory that returns a
 * `std::vector<std::unique_ptr<IPlacementEngine>>` and has zero concrete
 * engine knowledge in the daemon. Until that lands, this function
 * intentionally hardcodes the three modes.
 *
 * Pointer-type guide (for the daemon and its consumers):
 * - PlacementEngineBase* — when you need QObject signal connections
 *   (e.g. UnifiedLayoutController, WindowTrackingAdaptor).
 * - IPlacementEngine* — when you only call interface methods, no
 *   signals (e.g. ControlAdaptor, WindowDragAdaptor, SupportReport).
 *
 * @param layoutManager   Layout registry (borrowed, must outlive engines)
 * @param windowTracker   Window tracking service (borrowed)
 * @param screenManager   Screen manager (borrowed)
 * @param algorithmRegistry Tile-algorithm registry (borrowed)
 * @param zoneDetector    Zone detector for snap mode (borrowed)
 * @param settings        Settings instance (borrowed)
 * @param vdm             Virtual desktop manager (borrowed)
 * @param windowRegistry  Window registry for class lookups (borrowed)
 * @return EngineSet with all three objects constructed
 */
EngineSet createBuiltInEngines(PhosphorZones::LayoutRegistry* layoutManager,
                               PhosphorPlacement::WindowTrackingService* windowTracker,
                               Phosphor::Screens::ScreenManager* screenManager,
                               PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                               PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings,
                               PhosphorWorkspaces::VirtualDesktopManager* vdm,
                               PhosphorEngine::WindowRegistry* windowRegistry);

} // namespace PlasmaZones

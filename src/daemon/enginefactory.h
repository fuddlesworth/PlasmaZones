// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class QObject;

namespace PhosphorZones {
class LayoutRegistry;
class IZoneDetector;
} // namespace PhosphorZones

namespace PhosphorScreens {
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
class CrossSurfaceResolver;

struct EngineSet
{
    /// Shared neighbour-output / neighbour-desktop resolver injected into the
    /// engines. Declared FIRST so it is destroyed LAST (members destruct in
    /// reverse declaration order) — it must outlive the engines that borrow it.
    std::unique_ptr<CrossSurfaceResolver> crossSurfaceResolver;
    std::unique_ptr<PhosphorTileEngine::AutotileEngine> autotile;
    std::unique_ptr<PhosphorSnapEngine::SnapEngine> snap;
    std::unique_ptr<ScreenModeRouter> router;
};

/**
 * @brief Create both placement engines and the mode router.
 *
 * Concrete engine headers are included in the .cpp — the factory header
 * only forward-declares them. The caller (Daemon) must wire persistence
 * delegates, signal connections, and adaptor setup after receiving the
 * returned EngineSet.
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
 * @return EngineSet with all engines and the resolver constructed
 */
EngineSet createEngines(PhosphorZones::LayoutRegistry* layoutManager,
                        PhosphorPlacement::WindowTrackingService* windowTracker,
                        PhosphorScreens::ScreenManager* screenManager,
                        PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                        PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings,
                        PhosphorWorkspaces::VirtualDesktopManager* vdm, PhosphorEngine::WindowRegistry* windowRegistry);

} // namespace PlasmaZones

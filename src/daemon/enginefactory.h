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

namespace PlasmaZones {

class AutotileEngine;
class ISettings;
class ScreenModeRouter;
class SnapEngine;
class VirtualDesktopManager;
class WindowRegistry;
class WindowTrackingService;

/**
 * @brief Grouped result of createEngines().
 *
 * Returns concrete engine types so the daemon can wire adaptor construction
 * and engine-specific setup without immediately downcasting. The daemon
 * stores these as PlacementEngineBase* members for all subsequent code paths.
 */
struct EngineSet
{
    std::unique_ptr<AutotileEngine> autotile;
    std::unique_ptr<SnapEngine> snap;
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
 * @param parent          QObject parent for engine lifetime
 * @return EngineSet with all three objects constructed
 */
EngineSet createEngines(PhosphorZones::LayoutRegistry* layoutManager, WindowTrackingService* windowTracker,
                        Phosphor::Screens::ScreenManager* screenManager,
                        PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                        PhosphorZones::IZoneDetector* zoneDetector, ISettings* settings, VirtualDesktopManager* vdm,
                        WindowRegistry* windowRegistry, QObject* parent);

} // namespace PlasmaZones

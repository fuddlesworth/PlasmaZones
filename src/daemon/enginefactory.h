// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class QObject;

namespace PhosphorEngineApi {
class PlacementEngineBase;
}

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

class ISettings;
class ScreenModeRouter;
class VirtualDesktopManager;
class WindowRegistry;
class WindowTrackingService;

/**
 * @brief Grouped result of createEngines().
 *
 * Holds the two placement engines and the mode router. The caller
 * (Daemon) takes ownership via std::unique_ptr move semantics and
 * is responsible for all subsequent signal wiring and persistence
 * delegate setup.
 */
struct EngineSet
{
    std::unique_ptr<PhosphorEngineApi::PlacementEngineBase> autotile;
    std::unique_ptr<PhosphorEngineApi::PlacementEngineBase> snap;
    std::unique_ptr<ScreenModeRouter> router;
};

/**
 * @brief Create both placement engines and the mode router.
 *
 * Concrete engine headers are included here and in daemon.cpp (for
 * adaptor construction); all other consumers use abstract pointers.
 *
 * Pointer-type guide:
 * - PlacementEngineBase* — when you need QObject signal connections
 *   (e.g. UnifiedLayoutController, WindowTrackingAdaptor).
 * - IPlacementEngine* — when you only call interface methods, no
 *   signals (e.g. ControlAdaptor, WindowDragAdaptor, SupportReport).
 *
 * The engines are constructed with their mandatory dependencies. The
 * caller must wire persistence delegates, signal connections, and
 * adaptor setup after receiving the returned EngineSet.
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

namespace PhosphorEngine {
class WindowRegistry;
}
namespace PhosphorPlacement {
class WindowTrackingService;
}

namespace PlasmaZones {

/**
 * @brief Filter a tiling-family seed order's entries against live window state.
 *
 * Shared by both placing engines: Daemon::seedAutotileOrderForScreen and
 * Daemon::updateScrollingScreens seed from the same m_lastEngineOrders map and
 * need the same admission rule, so a window that must not come back as a tile
 * must not come back as a strip column either.
 *
 * Order sources describe past arrangements (the tiled order captured at
 * toggle-off, or zone assignments) and know nothing about what happened to
 * those windows since.
 *
 * Float is PER ENGINE, so a non-minimized window is always admitted: a live
 * float read at seed time belongs to the mode the screen is leaving, and the
 * durable snap slot's stateFloating is the window's SNAPPING-mode verdict —
 * neither is this engine's own float state. (Dropping on them made a
 * snap-floated window untileable by mode swap; the snap float still restores
 * on return to snapping because windowsReleased reads the snap slot, which
 * seeding never mutates.)
 *
 * Minimized windows are KEPT as positional placeholders — the engine's
 * strict-seed path defers adding them until their windowOpened arrives,
 * preserving position without a hidden window occupying a tile. The one DROP
 * is a user-floated-then-minimized window (instance-exact record with a
 * floating snap slot): a placeholder would tile it on unminimize instead of
 * restoring its float.
 *
 * Extracted from Daemon::seedAutotileOrderForScreen so the predicate is unit
 * testable without a full daemon.
 */
void filterEngineSeedOrder(QStringList& order, PhosphorPlacement::WindowTrackingService* wts,
                           const PhosphorEngine::WindowRegistry* registry);

} // namespace PlasmaZones

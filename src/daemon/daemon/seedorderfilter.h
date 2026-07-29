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
 * @brief Filter an autotile seed order's entries against live window state.
 *
 * Order sources describe past arrangements (the tiled order captured at
 * toggle-off, or zone assignments) and know nothing about what happened to
 * those windows since. Entries are DROPPED when the window must not be seeded
 * back as tiled:
 *  - it is floating NOW and not minimized (a float set by another mode would
 *    be tiled over — the strict seed only restores floating from the AUTOTILE
 *    placement slot);
 *  - its instance-exact durable placement record carries a floating snap slot
 *    (the mode flips before the second seed, so the live floating resolver
 *    then reads the empty autotile slot; the record preserves the source-mode
 *    user float across that flip).
 * Minimized windows are KEPT as positional placeholders — the engine's
 * strict-seed path defers adding them until their windowOpened arrives,
 * preserving position without a hidden window occupying a tile.
 *
 * Extracted from Daemon::seedAutotileOrderForScreen so the predicate is unit
 * testable without a full daemon.
 */
void filterAutotileSeedOrder(QStringList& order, PhosphorPlacement::WindowTrackingService* wts,
                             const PhosphorEngine::WindowRegistry* registry);

} // namespace PlasmaZones

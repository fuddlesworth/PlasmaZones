// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QRect>
#include <QStringList>

#include <functional>

namespace PhosphorEngine {
class IPlacementEngine;
class WindowRegistry;
}
namespace PhosphorPlacement {
class WindowTrackingService;
}
namespace PhosphorScreens {
class ScreenManager;
}

namespace PlasmaZones {

/// The context a seed is for, and where each window in the order is now. A
/// saved order names every window that was in the context when it left the
/// engine, including windows that have since moved to another screen or
/// desktop: seeding those would tile them back onto a screen they left
/// (F695). An empty @c screenId or a @c desktop below 1 skips that check.
struct SeedScope
{
    QString screenId;
    int desktop = 0;
    /// The screen a window is on now, empty when unknown (kept).
    std::function<QString(const QString& windowId)> screenOf;
};

/// Where @p windowId is now, as a seed judges it: the screen a tiling engine
/// holds it on in view, else snap's tracked screen, else the screen under
/// its last known frame. The engines answer first because a maximized
/// window's frame on a split monitor centres on the wrong virtual screen
/// (F715).
QString seedWindowScreen(const QString& windowId, const QList<const PhosphorEngine::IPlacementEngine*>& tilingEngines,
                         const PhosphorEngine::IPlacementEngine* snap, const QRect& frame,
                         const PhosphorScreens::ScreenManager* screens);

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
 * is a user-floated-then-minimized window: a placeholder would tile it on
 * unminimize instead of restoring its float.
 *
 * @p targetEngineId names the engine being seeded, and the minimized drop
 * reads THAT engine's durable slot. This parameter is load-bearing, not
 * cosmetic: reading a fixed slot here would re-break the per-engine float
 * invariant the non-minimized arm above is careful to honour. Seeding the
 * scroll engine while the window carries a floating SNAP slot must not drop
 * it (it has no scrolling float verdict, so it belongs in the strip), and
 * seeding after a scrolling float must not admit it just because its snap
 * slot happens to be clean.
 *
 * Every window, minimized or not, is first held to @p scope: one now on
 * another screen (compared at the virtual screen level), or on desktops that
 * do not include the seeded one, is dropped.
 *
 * Extracted from Daemon::seedAutotileOrderForScreen so the predicate is unit
 * testable without a full daemon.
 */
void filterEngineSeedOrder(QStringList& order, PhosphorPlacement::WindowTrackingService* wts,
                           const PhosphorEngine::WindowRegistry* registry, const QString& targetEngineId,
                           const SeedScope& scope);

} // namespace PlasmaZones

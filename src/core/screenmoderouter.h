// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngineApi/IPlacementEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "plasmazones_export.h"

#include <QString>
#include <QStringList>

namespace PhosphorZones {
class LayoutRegistry;
}

/**
 * @brief Single source of truth for "which engine owns screen X".
 *
 * Every window-lifecycle and cleanup entry point in the daemon and its
 * D-Bus adaptors should route through this class instead of calling
 * `m_autotileEngine->isAutotileScreen()` / `modeForScreen()` ad hoc at
 * each site. Engines trust that their callers routed correctly — no
 * defensive mode checks inside SnapEngine or AutotileEngine.
 *
 * Why: the daemon acquired ~20 copies of the same "if autotile, call
 * autotile; else call snap" branch across adaptor methods, resnap
 * paths, navigation handlers, and session restore. Every time one of
 * them was missed (e.g. `resolveWindowRestore` called SnapEngine
 * directly), a stale snap assignment could bleed into an autotile
 * screen and fight the engine that actually owned placement. Single
 * router + pure engines eliminates the whole class of bug.
 *
 * The router is a thin façade over existing lookups — it does not
 * hold any state of its own. Construction is cheap, it should be
 * held by the daemon and passed by reference/pointer to consumers.
 */
namespace PlasmaZones {

/**
 * @brief Single source of truth for "which engine owns screen X".
 *
 * Every window-lifecycle and cleanup entry point in the daemon and its
 * D-Bus adaptors should route through this class instead of calling
 * `m_autotileEngine->isAutotileScreen()` / `modeForScreen()` ad hoc at
 * each site. Engines trust that their callers routed correctly — no
 * defensive mode checks inside SnapEngine or AutotileEngine.
 *
 * The router is a thin facade over existing lookups — it does not
 * hold any state of its own. Construction is cheap, it should be
 * held by the daemon and passed by reference/pointer to consumers.
 */
class PLASMAZONES_EXPORT ScreenModeRouter
{
public:
    /// Construct with references to the engines (as IPlacementEngine*)
    /// and layout manager. None of the pointers are owned; they must
    /// outlive the router.
    ScreenModeRouter(PhosphorZones::LayoutRegistry* layoutManager, PhosphorEngineApi::IPlacementEngine* snapEngine,
                     PhosphorEngineApi::IPlacementEngine* autotileEngine);

    /// Current mode for @p screenId. Consults the autotile engine's
    /// live set first (mode is derived from assignment + context) and
    /// falls back to the layout manager's cascade for unknown screens.
    /// Returns Snapping when the screen isn't recognised — safest
    /// default since snap-mode operations are generally idempotent
    /// against missing state.
    PhosphorZones::AssignmentEntry::Mode modeFor(const QString& screenId) const;

    /// The engine that owns placement on @p screenId. Callers should treat
    /// the returned pointer as the only legitimate route into per-window
    /// behaviour for that screen — they must not reach into a specific
    /// engine directly.
    PhosphorEngineApi::IPlacementEngine* engineFor(const QString& screenId) const;

    /// Convenience predicates. @see modeFor for the fallback semantics.
    bool isSnapMode(const QString& screenId) const;
    bool isAutotileMode(const QString& screenId) const;

    /// Split a list of screen ids into snap-mode and autotile-mode
    /// buckets. Useful for multi-screen cleanup and resnap paths that
    /// need to iterate one engine at a time. Preserves input order
    /// within each bucket.
    struct Partitioned
    {
        QStringList snap;
        QStringList autotile;
    };
    Partitioned partitionByMode(const QStringList& screenIds) const;

private:
    PhosphorZones::LayoutRegistry* m_layoutManager;
    PhosphorEngineApi::IPlacementEngine* m_snapEngine;
    PhosphorEngineApi::IPlacementEngine* m_autotileEngine;
};

} // namespace PlasmaZones

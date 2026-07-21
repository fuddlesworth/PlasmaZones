// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {

/// Per-screen placement state contract.
///
/// Both snap-mode (zone assignments) and autotile-mode (tiling order)
/// implement this so the daemon's D-Bus adaptor and compositor plugins can read
/// state uniformly without branching on mode.
///
/// Deliberately NOT a persistence interface: neither state is written to disk.
/// What survives a restart is per-window (WindowPlacementStore), so a placement
/// state is rebuilt from those records rather than restored wholesale.
///
/// The interface is deliberately read-only. Mutation goes
/// through engine-specific APIs (SnapState::assignWindowToZone,
/// TilingState::addWindow, etc.) because the semantics diverge — the
/// daemon routes mutations through IPlacementEngine which delegates to
/// the correct concrete state object.
class PHOSPHORENGINE_EXPORT IPlacementState
{
public:
    virtual ~IPlacementState() = default;

    /// Screen this state object manages.
    virtual QString screenId() const = 0;

    /// Total number of managed windows (tiled + floating).
    virtual int windowCount() const = 0;

    /// All windows managed by this state (tiled + floating).
    virtual QStringList managedWindows() const = 0;

    /// Whether the window is in this state's managed set.
    virtual bool containsWindow(const QString& windowId) const = 0;

    /// Whether the window is floating (excluded from placement).
    virtual bool isFloating(const QString& windowId) const = 0;

    /// All currently-floating windows.
    virtual QStringList floatingWindows() const = 0;

    /// Opaque placement identifier for the window's current slot.
    /// Snap mode: zone UUID. Autotile mode: tiling-order index as string.
    /// Empty if the window is floating or unassigned.
    virtual QString placementIdForWindow(const QString& windowId) const = 0;

    /// Number of tiled (non-floating) windows in the managed set.
    virtual int tiledWindowCount() const
    {
        return 0;
    }

    /// Number of master windows (autotile concept; snap returns 1).
    virtual int masterCount() const
    {
        return 1;
    }
};

} // namespace PhosphorEngine

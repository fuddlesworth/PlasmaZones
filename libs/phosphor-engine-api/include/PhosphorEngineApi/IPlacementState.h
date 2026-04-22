// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PhosphorEngineApi {

/// Per-screen placement state contract.
///
/// Both snap-mode (zone assignments) and autotile-mode (tiling order)
/// implement this so the daemon's persistence layer, D-Bus adaptor, and
/// compositor plugins can read state uniformly without branching on mode.
///
/// The interface is deliberately read-only + serialization. Mutation goes
/// through engine-specific APIs (SnapState::assignWindowToZone,
/// TilingState::addWindow, etc.) because the semantics diverge — the
/// daemon routes mutations through IPlacementEngine which delegates to
/// the correct concrete state object.
class IPlacementState
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

    /// Serialize to JSON for session persistence.
    virtual QJsonObject toJson() const = 0;
};

} // namespace PhosphorEngineApi

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QString>

namespace PlasmaZones {

class Settings;

/**
 * @brief Scoped helper that performs virtual-screen swap and rotate
 *        operations against the authoritative Settings store.
 *
 * Both operations mutate the VirtualScreenConfig for a single physical
 * monitor — swap exchanges the @c region between two sibling VSs, rotate
 * cycles regions through all siblings in the physical monitor's own ring.
 * VS IDs and every other per-VS field are preserved, so downstream state
 * keyed on VS ID — windows, layouts, autotile state, assignment entries —
 * remains bound and simply follows the new geometry via the existing
 * Settings::virtualScreenConfigsChanged propagation.
 */
class PLASMAZONES_EXPORT VirtualScreenSwapper
{
public:
    explicit VirtualScreenSwapper(Settings* settings);

    /// Swap the region of @p currentVirtualScreenId with the adjacent sibling
    /// VS in the given @p direction within the same physical monitor.
    /// @p direction must match one of Utils::Direction::{Left,Right,Up,Down}.
    /// Returns true if the swap was applied and committed to Settings.
    /// Returns false if the current id is not a virtual screen, the physical
    /// monitor has fewer than two VSs, no sibling lies in the requested
    /// direction, or the Settings store rejects the mutated config.
    bool swapInDirection(const QString& currentVirtualScreenId, const QString& direction);

    /// Rotate all VS regions on @p physicalScreenId in spatial clockwise
    /// ring order. @p clockwise follows the same convention as
    /// WindowTrackingService::calculateRotation — each VS "moves forward"
    /// in the ring when clockwise=true.
    /// Returns true if the rotation was applied and committed to Settings.
    /// Returns false if the physical monitor has fewer than two VSs or the
    /// Settings store rejects the mutated config.
    bool rotate(const QString& physicalScreenId, bool clockwise);

private:
    Settings* m_settings;
};

} // namespace PlasmaZones

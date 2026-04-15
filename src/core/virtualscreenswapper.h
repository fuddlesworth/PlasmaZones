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
    /// Structured outcome for swap/rotate operations. Lets callers
    /// distinguish "geometry changed" from each rejection reason so the
    /// OSD / D-Bus layer can emit specific failure feedback instead of
    /// a generic boolean.
    enum class Result {
        Ok, ///< Mutation applied and committed to Settings.
        NotVirtual, ///< Caller passed a physical id where a VS id was required (or vice versa).
        NoSubdivision, ///< The physical monitor has fewer than two virtual screens.
        UnknownVirtualScreen, ///< The VS id is well-formed but not present in the current config.
        NoSiblingInDirection, ///< Swap: no sibling VS lies in the requested direction.
        InvalidDirection, ///< Swap: direction string was empty or unrecognised.
        SettingsRejected, ///< Settings::setVirtualScreenConfig rejected the mutated config.
    };

    explicit VirtualScreenSwapper(Settings* settings);

    /// Swap the region of @p currentVirtualScreenId with the adjacent sibling
    /// VS in the given @p direction within the same physical monitor.
    /// @p direction must match one of Utils::Direction::{Left,Right,Up,Down}.
    Result swapInDirection(const QString& currentVirtualScreenId, const QString& direction);

    /// Rotate all VS regions on @p physicalScreenId. The ring order is the
    /// spatial clockwise sort of VS centres around the physical monitor's
    /// centroid; for 1D strips (all centres collinear) the helper falls back
    /// to a sort along the varying axis so the visual result matches user
    /// expectation. @p clockwise follows VirtualScreenConfig::rotateRegions.
    Result rotate(const QString& physicalScreenId, bool clockwise);

    /// Translate a Result to a stable string token suitable for OSD reasons
    /// and D-Bus logs. Empty string for Ok.
    static QString reasonString(Result result);

private:
    Settings* m_settings;
};

} // namespace PlasmaZones

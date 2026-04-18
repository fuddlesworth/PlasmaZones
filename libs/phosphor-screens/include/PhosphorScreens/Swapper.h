// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorscreens_export.h"

#include <QString>

namespace Phosphor::Screens {

class IConfigStore;

/**
 * @brief Canonical direction tokens accepted by VirtualScreenSwapper.
 *
 * Lower-case ASCII matches the D-Bus wire format used by the PlasmaZones
 * `org.plasmazones.Screen.swapVirtualScreenInDirection` method, so the
 * adaptor can pass through the user's string verbatim. New callers should
 * prefer constructing direction strings via these constants instead of
 * hand-rolled string literals.
 */
namespace Direction {
inline constexpr QLatin1StringView Left{"left"};
inline constexpr QLatin1StringView Right{"right"};
inline constexpr QLatin1StringView Up{"up"};
inline constexpr QLatin1StringView Down{"down"};
} // namespace Direction

/**
 * @brief Scoped helper that performs virtual-screen swap and rotate
 *        operations against an injected IConfigStore.
 *
 * Both operations mutate the VirtualScreenConfig for a single physical
 * monitor. Swap exchanges the @c region between two sibling VSs; rotate
 * cycles regions through all siblings on the physical monitor's spatial
 * clockwise ring. VS IDs and every other per-VS field are preserved, so
 * downstream state keyed on VS ID — windows, layouts, autotile state,
 * assignment entries — remains bound and simply follows the new geometry
 * via the host's @ref IConfigStore::changed propagation.
 *
 * Lifetime: the swapper does not own the store. Caller must keep both
 * alive for the duration of any swap/rotate call.
 */
class PHOSPHORSCREENS_EXPORT VirtualScreenSwapper
{
public:
    /// Structured outcome for swap/rotate operations. Lets callers
    /// distinguish "geometry changed" from each rejection reason so the
    /// OSD / D-Bus layer can emit specific failure feedback instead of a
    /// generic boolean.
    enum class Result {
        Ok, ///< Mutation applied and committed via IConfigStore::save.
        NotVirtual, ///< Caller passed a physical id where a VS id was required (or vice versa).
        /// The physical monitor has fewer than two virtual screens.
        /// NOTE: deliberately conflated with "physical id not in store" — an
        /// unknown physId returns an empty config from the store which fails
        /// the same size-check and lands here. Callers that need to
        /// distinguish the two cases should cross-check existence
        /// against ScreenManager first.
        NoSubdivision,
        UnknownVirtualScreen, ///< The VS id is well-formed but not present in the current config.
        NoSiblingInDirection, ///< Swap: no sibling VS lies in the requested direction.
        InvalidDirection, ///< Swap: direction string was empty or unrecognised.
        /// In-memory swap/rotate step failed (duplicate ids, ids not in the
        /// working config). Distinct from SettingsRejected because nothing
        /// was ever handed to the store — the caller's input is the problem.
        SwapFailed,
        SettingsRejected, ///< IConfigStore::save rejected the mutated config.
    };

    explicit VirtualScreenSwapper(IConfigStore* store);

    /// Swap the region of @p currentVirtualScreenId with the adjacent
    /// sibling VS in the given @p direction within the same physical
    /// monitor. @p direction must match one of @ref Direction::{Left,
    /// Right, Up, Down}.
    Result swapInDirection(const QString& currentVirtualScreenId, const QString& direction);

    /// Rotate all VS regions on @p physicalScreenId. The ring order is the
    /// spatial clockwise sort of VS centres around the physical monitor's
    /// centroid; for 1D strips (all centres collinear) the helper falls
    /// back to a sort along the varying axis so the visual result matches
    /// user expectation. @p clockwise follows
    /// @ref VirtualScreenConfig::rotateRegions.
    Result rotate(const QString& physicalScreenId, bool clockwise);

    /// Translate a Result to a stable string token suitable for OSD
    /// reasons and D-Bus logs. Empty string for Ok.
    static QString reasonString(Result result);

private:
    IConfigStore* m_store;
};

} // namespace Phosphor::Screens

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>
#include <QString>

namespace PhosphorIdentity {

/**
 * @brief Virtual-screen ID format utilities.
 *
 * Stable cross-process format for the IDs that identify a sub-region of a
 * physical monitor when the user has subdivided it: `"<physicalId>/vs:<index>"`.
 *
 * Same role as @ref WindowId for window identity — a single source of truth
 * for the wire-format string. Daemon, KWin effect, KCM, and any future
 * compositor plugin must spell it the same way; this header is the place to
 * change that spelling if it ever needs to change.
 *
 * Header-only and `inline` throughout: no link cost, safe to include
 * everywhere identity strings are handled.
 */
namespace VirtualScreenId {

/// Separator between physical screen ID and virtual index.
inline constexpr QLatin1String Separator{"/vs:"};

/// Check if a screen ID is a virtual screen ID (contains "/vs:").
inline bool isVirtual(const QString& screenId)
{
    int pos = screenId.indexOf(Separator);
    return pos > 0; // Must have non-empty physical ID before separator
}

/// Extract the physical screen ID from a virtual screen ID.
/// Returns the original ID if not a virtual screen ID.
inline QString extractPhysicalId(const QString& screenId)
{
    int sep = screenId.indexOf(Separator);
    return (sep > 0) ? screenId.left(sep) : screenId;
}

/// Extract the virtual screen index from a virtual screen ID.
/// Returns -1 if not a virtual screen ID.
inline int extractIndex(const QString& screenId)
{
    int sep = screenId.indexOf(Separator);
    if (sep <= 0) {
        return -1;
    }
    bool ok = false;
    int index = screenId.mid(sep + Separator.size()).toInt(&ok);
    return (ok && index >= 0) ? index : -1;
}

/// Construct a virtual screen ID from physical ID and index.
/// @pre index must be >= 0; negative indices return an empty string.
inline QString make(const QString& physicalScreenId, int index)
{
    if (physicalScreenId.isEmpty() || index < 0) {
        return {};
    }
    return physicalScreenId + Separator + QString::number(index);
}

/// Check if two screen IDs share the same physical screen.
/// For virtual screens (containing "/vs:"), strips the suffix before comparing.
inline bool samePhysical(const QString& idA, const QString& idB)
{
    return extractPhysicalId(idA) == extractPhysicalId(idB);
}

/// Detect a virtual-screen crossing: the screen IDs differ, but both belong
/// to the same physical monitor. Returns false when both IDs are plain
/// physical IDs (outputChanged handles those) or when they belong to
/// different physical monitors.
inline bool isVirtualScreenCrossing(const QString& oldScreenId, const QString& newScreenId)
{
    if (oldScreenId.isEmpty() || newScreenId.isEmpty() || oldScreenId == newScreenId) {
        return false;
    }
    // At least one side must be a virtual ID — two plain physical IDs that
    // differ are a physical monitor change, not a VS crossing.
    if (!isVirtual(oldScreenId) && !isVirtual(newScreenId)) {
        return false;
    }
    return samePhysical(oldScreenId, newScreenId);
}

} // namespace VirtualScreenId

} // namespace PhosphorIdentity

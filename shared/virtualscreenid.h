// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1StringView>
#include <QString>

namespace PlasmaZones {

/// Virtual screen ID format utilities
///
/// Shared between the KWin effect and the daemon. This header is intentionally
/// header-only and depends only on Qt types so both targets can include it
/// without pulling in each other's headers.
namespace VirtualScreenId {

/// Separator between physical screen ID and virtual index
inline QLatin1StringView separator()
{
    return QLatin1StringView("/vs:");
}

/// Check if a screen ID is a virtual screen ID (contains "/vs:")
inline bool isVirtual(const QString& screenId)
{
    int pos = screenId.indexOf(separator());
    return pos > 0; // Must have non-empty physical ID before separator
}

/// Extract the physical screen ID from a virtual screen ID
/// Returns the original ID if not a virtual screen ID
inline QString extractPhysicalId(const QString& screenId)
{
    int sep = screenId.indexOf(separator());
    return (sep > 0) ? screenId.left(sep) : screenId;
}

/// Extract the virtual screen index from a virtual screen ID
/// Returns -1 if not a virtual screen ID
inline int extractIndex(const QString& screenId)
{
    int sep = screenId.indexOf(separator());
    if (sep < 0) {
        return -1;
    }
    bool ok = false;
    int index = screenId.mid(sep + separator().size()).toInt(&ok);
    return (ok && index >= 0) ? index : -1;
}

/// Construct a virtual screen ID from physical ID and index
/// @pre index must be >= 0; negative indices return an empty string
inline QString make(const QString& physicalScreenId, int index)
{
    if (physicalScreenId.isEmpty() || index < 0) {
        return {};
    }
    return physicalScreenId + separator() + QString::number(index);
}

/// Check if two screen IDs share the same physical screen.
/// For virtual screens (containing "/vs:"), strips the suffix before comparing.
inline bool samePhysical(const QString& idA, const QString& idB)
{
    return extractPhysicalId(idA) == extractPhysicalId(idB);
}

} // namespace VirtualScreenId

} // namespace PlasmaZones

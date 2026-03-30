// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

namespace PlasmaZones {

/**
 * @brief Definition of a single virtual screen within a physical screen
 *
 * A virtual screen represents a rectangular sub-region of a physical monitor.
 * Each virtual screen gets its own screen ID, layout assignments, autotile state,
 * overlay windows, and all other per-screen functionality.
 */
struct PLASMAZONES_EXPORT VirtualScreenDef
{
    QString id; ///< Full ID: "physicalId/vs:N"
    QString physicalScreenId; ///< Owning physical screen's stable EDID ID
    QString displayName; ///< User-facing name, e.g. "Left", "Right"
    QRectF region; ///< Relative geometry within physical screen (0-1)
    int index = 0; ///< Index within the physical screen's subdivision

    /// Shared tolerance for floating-point region comparisons.
    /// Used by isValid() and physicalEdges() to handle serialization precision loss.
    static constexpr qreal Tolerance = 1e-3;

    /// Compute absolute geometry from the physical screen's geometry
    /// Uses edge-consistent rounding to avoid 1px gaps/overlaps between adjacent screens
    QRect absoluteGeometry(const QRect& physicalGeometry) const
    {
        int left = physicalGeometry.x() + qRound(region.x() * physicalGeometry.width());
        int top = physicalGeometry.y() + qRound(region.y() * physicalGeometry.height());
        int right = physicalGeometry.x() + qRound((region.x() + region.width()) * physicalGeometry.width());
        int bottom = physicalGeometry.y() + qRound((region.y() + region.height()) * physicalGeometry.height());
        // Clamp to physical screen bounds to prevent tolerance overshoot
        right = qMin(right, physicalGeometry.x() + physicalGeometry.width());
        bottom = qMin(bottom, physicalGeometry.y() + physicalGeometry.height());
        left = qMax(left, physicalGeometry.x());
        top = qMax(top, physicalGeometry.y());
        return QRect(left, top, right - left, bottom - top);
    }

    bool operator==(const VirtualScreenDef& other) const
    {
        auto fuzzyEqual = [](qreal a, qreal b) {
            return qAbs(a - b) < Tolerance;
        };
        auto fuzzyRectEqual = [&fuzzyEqual](const QRectF& a, const QRectF& b) {
            return fuzzyEqual(a.x(), b.x()) && fuzzyEqual(a.y(), b.y()) && fuzzyEqual(a.width(), b.width())
                && fuzzyEqual(a.height(), b.height());
        };
        return id == other.id && physicalScreenId == other.physicalScreenId && displayName == other.displayName
            && fuzzyRectEqual(region, other.region) && index == other.index;
    }

    /// Check if the definition is valid: non-empty id, non-empty physicalScreenId,
    /// non-negative origin, non-zero size, region within [0,1] bounds.
    /// Uses Tolerance to handle float serialization precision loss.
    bool isValid() const
    {
        return !id.isEmpty() && !physicalScreenId.isEmpty() && region.x() >= -Tolerance && region.y() >= -Tolerance
            && region.width() > 0 && region.height() > 0 && region.x() + region.width() <= 1.0 + Tolerance
            && region.y() + region.height() <= 1.0 + Tolerance;
    }

    bool operator!=(const VirtualScreenDef& other) const
    {
        return !(*this == other);
    }

    /// Check which edges of this virtual screen are at the physical screen boundary
    /// (vs internal edges shared with another virtual screen).
    /// An edge at the physical boundary should get outer gaps;
    /// an internal edge should get inner gap (like zone padding) to avoid double gaps.
    struct PhysicalEdges
    {
        bool left = true;
        bool top = true;
        bool right = true;
        bool bottom = true;
    };
    PhysicalEdges physicalEdges() const
    {
        return {region.left() < Tolerance, region.top() < Tolerance, region.right() > (1.0 - Tolerance),
                region.bottom() > (1.0 - Tolerance)};
    }
};

/**
 * @brief Configuration for how a physical screen is subdivided into virtual screens
 *
 * When screens is empty, the physical screen has no subdivisions and acts as
 * a single implicit virtual screen (backward-compatible default).
 */
struct PLASMAZONES_EXPORT VirtualScreenConfig
{
    QString physicalScreenId;
    QVector<VirtualScreenDef> screens;

    bool hasSubdivisions() const
    {
        return screens.size() > 1;
    }
    bool isEmpty() const
    {
        return screens.isEmpty();
    }

    bool operator==(const VirtualScreenConfig& other) const
    {
        return physicalScreenId == other.physicalScreenId && screens == other.screens;
    }

    bool operator!=(const VirtualScreenConfig& other) const
    {
        return !(*this == other);
    }
};

/// Virtual screen ID format utilities
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
inline QString make(const QString& physicalScreenId, int index)
{
    return physicalScreenId + separator() + QString::number(index);
}

} // namespace VirtualScreenId

} // namespace PlasmaZones

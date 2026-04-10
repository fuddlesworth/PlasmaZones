// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

#include "shared/virtualscreenid.h"

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
        // Prevent degenerate geometry when tolerance overshoot pushes left/top past right/bottom.
        // Apply BEFORE floor clamp so the floor clamp has the final word on minimum origin.
        left = qMin(left, right - 1);
        top = qMin(top, bottom - 1);
        left = qMax(left, physicalGeometry.x());
        top = qMax(top, physicalGeometry.y());
        int w = qMax(1, right - left);
        int h = qMax(1, bottom - top);
        return QRect(left, top, w, h);
    }

    /// Tolerance-aware equality for change detection (skip-if-unchanged guards).
    /// Uses Tolerance for region comparison to avoid spurious change signals
    /// when a config round-trips through JSON serialization.
    bool operator==(const VirtualScreenDef& other) const
    {
        return id == other.id && physicalScreenId == other.physicalScreenId && displayName == other.displayName
            && index == other.index && qAbs(region.x() - other.region.x()) < Tolerance
            && qAbs(region.y() - other.region.y()) < Tolerance
            && qAbs(region.width() - other.region.width()) < Tolerance
            && qAbs(region.height() - other.region.height()) < Tolerance;
    }

    /// Check if the definition is valid: non-empty id, non-empty physicalScreenId,
    /// non-negative origin, non-zero size, region within [0,1] bounds.
    /// Uses Tolerance to handle float serialization precision loss.
    /// Note: slightly negative coordinates (within -Tolerance) are accepted and
    /// clamped to zero by absoluteGeometry(). Always use absoluteGeometry() to
    /// obtain pixel coordinates rather than consuming region directly.
    bool isValid() const
    {
        return VirtualScreenId::isVirtual(id) && !physicalScreenId.isEmpty() && index >= 0 && region.x() >= -Tolerance
            && region.y() >= -Tolerance && region.width() > 0 && region.height() > 0
            && region.x() + region.width() <= 1.0 + Tolerance && region.y() + region.height() <= 1.0 + Tolerance;
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

    bool operator==(const VirtualScreenConfig&) const = default;
};

} // namespace PlasmaZones

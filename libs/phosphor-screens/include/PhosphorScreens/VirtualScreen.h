// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QHash>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <utility>

namespace Phosphor::Screens {

/**
 * @brief Definition of a single virtual screen within a physical screen.
 *
 * A virtual screen represents a rectangular sub-region of a physical monitor.
 * Each virtual screen gets its own screen ID, layout assignments, autotile
 * state, overlay windows, and all other per-screen functionality.
 */
struct VirtualScreenDef
{
    QString id; ///< Full ID: "physicalId/vs:N"
    QString physicalScreenId; ///< Owning physical screen's stable EDID ID
    QString displayName; ///< User-facing name, e.g. "Left", "Right"
    QRectF region; ///< Relative geometry within physical screen (0-1)
    int index = 0; ///< Index within the physical screen's subdivision

    /// Shared tolerance for floating-point region comparisons.
    /// Used by isValid() and physicalEdges() to handle serialization precision loss.
    static constexpr qreal Tolerance = 1e-3;

    /// Compute absolute geometry from the physical screen's geometry.
    /// Uses edge-consistent rounding to avoid 1px gaps/overlaps between
    /// adjacent screens.
    QRect absoluteGeometry(const QRect& physicalGeometry) const
    {
        int left = physicalGeometry.x() + qRound(region.x() * physicalGeometry.width());
        int top = physicalGeometry.y() + qRound(region.y() * physicalGeometry.height());
        int right = physicalGeometry.x() + qRound((region.x() + region.width()) * physicalGeometry.width());
        int bottom = physicalGeometry.y() + qRound((region.y() + region.height()) * physicalGeometry.height());
        // Clamp to physical screen bounds to prevent tolerance overshoot.
        right = qMin(right, physicalGeometry.x() + physicalGeometry.width());
        bottom = qMin(bottom, physicalGeometry.y() + physicalGeometry.height());
        // Prevent degenerate geometry when tolerance overshoot pushes left/top
        // past right/bottom. Apply BEFORE the floor clamp so the floor clamp
        // has the final word on minimum origin.
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
    ///
    /// NOTE: the fuzzy region compare is deliberately non-transitive
    /// (a==b ∧ b==c does NOT imply a==c when region deltas chain across
    /// the tolerance window). Safe for change detection, but do NOT use
    /// VirtualScreenDef as a QHash/std::set key — hashed containers rely
    /// on transitivity. Equal-to-hash keys should be computed off the
    /// `id` field, which is exact.
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
        return PhosphorIdentity::VirtualScreenId::isVirtual(id) && !physicalScreenId.isEmpty() && index >= 0
            && region.x() >= -Tolerance && region.y() >= -Tolerance && region.width() > 0 && region.height() > 0
            && region.x() + region.width() <= 1.0 + Tolerance && region.y() + region.height() <= 1.0 + Tolerance;
    }

    bool operator!=(const VirtualScreenDef& other) const
    {
        return !(*this == other);
    }

    /// Check which edges of this virtual screen are at the physical screen
    /// boundary (vs internal edges shared with another virtual screen).
    /// An edge at the physical boundary should get outer gaps; an internal
    /// edge should get inner gap (like zone padding) to avoid double gaps.
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
 * @brief Configuration for how a physical screen is subdivided into virtual
 *        screens.
 *
 * When screens is empty, the physical screen has no subdivisions and acts as
 * a single implicit virtual screen (backward-compatible default).
 */
struct VirtualScreenConfig
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

    /// Exact vector equality: order-sensitive (QVector::operator==). Two
    /// configs with the same VS defs in different array orders compare
    /// as NOT equal, even though they describe identical topology. The
    /// `regionsOnly` detection in ScreenManager::setVirtualScreenConfig
    /// is the order-insensitive path that catches reordered-but-identical
    /// configs and routes them through the cheaper regions-only signal.
    bool operator==(const VirtualScreenConfig&) const = default;

    /// Validate geometric and structural invariants for this config.
    /// Returns true if the config is acceptable to apply (including the
    /// empty/removal case — empty configs are always valid).
    /// On failure, writes a single human-readable reason to *error.
    ///
    /// Centralizing the checks here lets both Settings (writing the source
    /// of truth) and ScreenManager (refreshing its cache) reject the same
    /// invalid inputs the same way, avoiding the divergence where Settings
    /// stores invalid data that ScreenManager later refuses to apply.
    static bool isValid(const VirtualScreenConfig& cfg, const QString& expectedPhysicalScreenId,
                        int maxScreensPerPhysical, QString* error = nullptr)
    {
        auto fail = [&](const QString& msg) {
            if (error)
                *error = msg;
            return false;
        };

        // Empty config = removal request. Always valid.
        if (cfg.isEmpty()) {
            return true;
        }

        if (!expectedPhysicalScreenId.isEmpty() && cfg.physicalScreenId != expectedPhysicalScreenId) {
            return fail(QStringLiteral("config physicalScreenId '%1' does not match parameter '%2'")
                            .arg(cfg.physicalScreenId, expectedPhysicalScreenId));
        }

        if (cfg.screens.size() < 2) {
            return fail(QStringLiteral("need at least 2 screens for subdivision, got %1").arg(cfg.screens.size()));
        }

        if (maxScreensPerPhysical > 0 && cfg.screens.size() > maxScreensPerPhysical) {
            return fail(QStringLiteral("too many virtual screens %1 (max %2)")
                            .arg(cfg.screens.size())
                            .arg(maxScreensPerPhysical));
        }

        QSet<QString> seenIds;
        QSet<int> seenIndices;
        for (const auto& def : cfg.screens) {
            if (!def.isValid()) {
                return fail(QStringLiteral("invalid VirtualScreenDef id='%1' region=(%2,%3 %4x%5)")
                                .arg(def.id)
                                .arg(def.region.x())
                                .arg(def.region.y())
                                .arg(def.region.width())
                                .arg(def.region.height()));
            }
            if (def.physicalScreenId != cfg.physicalScreenId) {
                return fail(QStringLiteral("def.physicalScreenId '%1' does not match config '%2' for def '%3'")
                                .arg(def.physicalScreenId, cfg.physicalScreenId, def.id));
            }
            const QString expectedId = PhosphorIdentity::VirtualScreenId::make(cfg.physicalScreenId, def.index);
            if (def.id != expectedId) {
                return fail(QStringLiteral("def.id '%1' does not match expected '%2' for index %3")
                                .arg(def.id, expectedId)
                                .arg(def.index));
            }
            if (seenIds.contains(def.id)) {
                return fail(QStringLiteral("duplicate def.id %1").arg(def.id));
            }
            seenIds.insert(def.id);
            if (seenIndices.contains(def.index)) {
                return fail(QStringLiteral("duplicate def.index %1").arg(def.index));
            }
            seenIndices.insert(def.index);
        }

        // Pairwise overlap check (tolerance-aware).
        for (int i = 0; i < cfg.screens.size(); ++i) {
            for (int j = i + 1; j < cfg.screens.size(); ++j) {
                const QRectF intersection = cfg.screens[i].region.intersected(cfg.screens[j].region);
                if (intersection.width() > VirtualScreenDef::Tolerance
                    && intersection.height() > VirtualScreenDef::Tolerance) {
                    return fail(QStringLiteral("overlapping regions between '%1' and '%2'")
                                    .arg(cfg.screens[i].id, cfg.screens[j].id));
                }
            }
        }

        // Total area must approximately cover the unit square.
        qreal totalArea = 0.0;
        for (const auto& def : cfg.screens) {
            totalArea += def.region.width() * def.region.height();
        }
        const qreal lower = 1.0 - VirtualScreenDef::Tolerance;
        const qreal upper = 1.0 + VirtualScreenDef::Tolerance;
        if (totalArea < lower) {
            return fail(QStringLiteral("insufficient coverage for '%1' — total area %2 < %3")
                            .arg(cfg.physicalScreenId)
                            .arg(totalArea)
                            .arg(lower));
        }
        if (totalArea > upper) {
            return fail(QStringLiteral("excessive coverage for '%1' — total area %2 > %3")
                            .arg(cfg.physicalScreenId)
                            .arg(totalArea)
                            .arg(upper));
        }

        return true;
    }

    /// Swap the @c region fields of the two defs identified by @p idA and
    /// @p idB. All other fields (id, physicalScreenId, displayName, index)
    /// are preserved so downstream state keyed on VS ID — windows, layouts,
    /// autotile state — remains valid and follows the new geometry.
    /// Returns false if either id is absent or both ids reference the same def.
    bool swapRegions(const QString& idA, const QString& idB)
    {
        if (idA == idB) {
            return false;
        }
        int indexA = -1;
        int indexB = -1;
        for (int i = 0; i < screens.size(); ++i) {
            if (screens[i].id == idA) {
                indexA = i;
            } else if (screens[i].id == idB) {
                indexB = i;
            }
        }
        if (indexA < 0 || indexB < 0) {
            return false;
        }
        std::swap(screens[indexA].region, screens[indexB].region);
        return true;
    }

    /// Rotate the @c region fields through the defs identified by @p orderedIds.
    /// Convention matches WindowTrackingService::calculateRotation — with
    /// @p clockwise = true, the def at position i in the ring inherits the
    /// region of its successor (position (i+1) mod n); with @p clockwise =
    /// false, it inherits the region of its predecessor (position (i-1) mod n).
    ///
    /// Read another way: on a CW rotation, each region's *content* moves
    /// **backward** one slot in the ordered ring (the region that was at
    /// def[1] now sits at def[0], so callers walking the ring in order see
    /// content shifting toward index 0 with the index-0 region wrapping to
    /// position n-1). When @p orderedIds is constructed from a spatial CW
    /// angle sort of a 2D grid, this matches the visual expectation that
    /// "CW rotate" cycles content clockwise around the ring.
    ///
    /// IDs and all other def fields are preserved. @p orderedIds may be a
    /// subset of the config's defs so callers can rotate only a subset.
    /// Returns false if @p orderedIds has fewer than two entries, any id is
    /// not found in the config, or any id appears more than once (duplicates
    /// would cause two ring slots to share a target def index — the rotation
    /// loop would then overwrite the same def twice with different sources
    /// and silently corrupt geometry).
    bool rotateRegions(const QVector<QString>& orderedIds, bool clockwise)
    {
        if (orderedIds.size() < 2) {
            return false;
        }
        QSet<QString> seenIds;
        seenIds.reserve(orderedIds.size());
        QVector<int> defIndices;
        defIndices.reserve(orderedIds.size());
        for (const auto& id : orderedIds) {
            if (seenIds.contains(id)) {
                return false; // duplicate id in orderedIds
            }
            seenIds.insert(id);
            int found = -1;
            for (int i = 0; i < screens.size(); ++i) {
                if (screens[i].id == id) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                return false;
            }
            defIndices.append(found);
        }
        const int n = defIndices.size();
        QVector<QRectF> regions;
        regions.reserve(n);
        for (int idx : defIndices) {
            regions.append(screens[idx].region);
        }
        for (int i = 0; i < n; ++i) {
            const int src = clockwise ? (i + 1) % n : (i - 1 + n) % n;
            screens[defIndices[i]].region = regions[src];
        }
        return true;
    }
};

} // namespace Phosphor::Screens

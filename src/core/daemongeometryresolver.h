// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorPlacement/IGeometryResolver.h>

#include <QString>
#include <QVariantMap>

#include <functional>

#include "geometryutils.h"

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Effective gap values for a screen plus which cascade layer supplied each,
/// for the read-only "what wins here?" provenance query. Computed off the SAME
/// inputs as resolveZonePadding / resolveOuterGaps (same context-rule override),
/// so the inspector matches actual geometry.
struct GapProvenance
{
    int zonePadding = 0;
    GeometryUtils::GapLayer zonePaddingLayer = GeometryUtils::GapLayer::Default;
    PhosphorLayout::EdgeGaps outerGaps;
    GeometryUtils::GapLayer outerGapsLayer = GeometryUtils::GapLayer::Default;
};

/// Bridges the daemon's ISettings + window-rule context resolution to the
/// placement library's IGeometryResolver. Gaps resolve through the cascade
/// context-rule override → per-screen → layout → global → default. The
/// context-rule layer is resolved for the CURRENT virtual desktop + activity
/// (supplied via callbacks so this stays free of a workspaces dependency),
/// consistent with the current-desktop occupancy filter already used at the
/// snap-geometry call sites.
class DaemonGeometryResolver : public PhosphorPlacement::IGeometryResolver
{
public:
    explicit DaemonGeometryResolver(ISettings* settings, PhosphorZones::LayoutRegistry* layoutRegistry = nullptr,
                                    std::function<int(const QString&)> currentVirtualDesktop = {},
                                    std::function<QString()> currentActivity = {})
        : m_settings(settings)
        , m_layoutRegistry(layoutRegistry)
        , m_currentVirtualDesktop(std::move(currentVirtualDesktop))
        , m_currentActivity(std::move(currentActivity))
    {
    }

    int resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const override;
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const override;
    int defaultBorderWidth() const override;
    int defaultBorderRadius() const override;

    /// Resolve the effective gaps for @p screenId (using @p layout for the
    /// layout layer) AND which cascade layer supplied each value. Reuses the
    /// exact inputs of resolveZonePadding / resolveOuterGaps.
    GapProvenance resolveGapProvenance(PhosphorZones::Layout* layout, const QString& screenId) const;

private:
    /// Build a PerScreenSnappingKey-shaped override map from the context rules
    /// resolved for @p screenId in the current desktop/activity. Empty when no
    /// gap rule matches (so the geometry cascade falls through to per-screen).
    QVariantMap contextGapOverrideFor(const QString& screenId) const;

    ISettings* m_settings;
    PhosphorZones::LayoutRegistry* m_layoutRegistry;
    std::function<int(const QString&)> m_currentVirtualDesktop;
    std::function<QString()> m_currentActivity;
};

} // namespace PlasmaZones

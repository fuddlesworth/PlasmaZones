// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemongeometryresolver.h"
#include "geometryutils.h"
#include "isettings.h"
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ZoneDefaults.h>

namespace PlasmaZones {

QVariantMap DaemonGeometryResolver::contextGapOverrideFor(const QString& screenId) const
{
    if (!m_layoutRegistry || screenId.isEmpty()) {
        return {};
    }
    const int virtualDesktop = m_currentVirtualDesktop ? m_currentVirtualDesktop(screenId) : 0;
    const QString activity = m_currentActivity ? m_currentActivity() : QString();
    // Translation to the PerScreenSnappingKey-shaped map is shared with the
    // preview/query geometry helpers via GeometryUtils::contextGapOverrideMap.
    // This is the snap-commit geometry path, so resolve against the "snapping"
    // placement mode — a per-mode `Mode Equals "snapping"` gap rule applies here.
    return GeometryUtils::contextGapOverrideMap(
        m_layoutRegistry->resolveContextGaps(screenId, virtualDesktop, activity, QStringLiteral("snapping")));
}

int DaemonGeometryResolver::resolveInnerGap(PhosphorZones::Layout* layout, const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorEngine::GeometryDefaults::InnerGap;
    }
    return GeometryUtils::getEffectiveInnerGap(layout, m_settings, contextGapOverrideFor(screenId));
}

PhosphorLayout::EdgeGaps DaemonGeometryResolver::resolveOuterGaps(PhosphorZones::Layout* layout,
                                                                  const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    }
    return GeometryUtils::getEffectiveOuterGaps(layout, m_settings, contextGapOverrideFor(screenId));
}

int DaemonGeometryResolver::defaultBorderWidth() const
{
    // Fall back to the canonical zone default (same source GeometryUtils uses)
    // rather than a magic number, for the degenerate no-settings case.
    return m_settings ? m_settings->borderWidth() : ::PhosphorZones::ZoneDefaults::BorderWidth;
}

int DaemonGeometryResolver::defaultBorderRadius() const
{
    return m_settings ? m_settings->borderRadius() : ::PhosphorZones::ZoneDefaults::BorderRadius;
}

int DaemonGeometryResolver::snapBorderInset() const
{
    // No inset. The KWin effect's border shader recolours the window's OWN
    // outermost band (inside the frame), for decorated and borderless windows
    // alike, so the border never extends past the frame edge into the neighbour.
    // A snapped window therefore fills its zone exactly; any visible separation
    // between tiles must come from the zone gap/padding settings, not from a
    // border-width inset (which previously assumed the border was drawn OUTSIDE
    // the frame and added a spurious 2x-border-width gap between tiles).
    return 0;
}

} // namespace PlasmaZones

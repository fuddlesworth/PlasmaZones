// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemongeometryresolver.h"
#include "geometryutils.h"
#include "isettings.h"
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

QVariantMap DaemonGeometryResolver::contextGapOverrideFor(const QString& screenId) const
{
    if (!m_layoutRegistry || screenId.isEmpty()) {
        return {};
    }
    const int virtualDesktop = m_currentVirtualDesktop ? m_currentVirtualDesktop() : 0;
    const QString activity = m_currentActivity ? m_currentActivity() : QString();
    // Translation to the PerScreenSnappingKey-shaped map is shared with the
    // preview/query geometry helpers via GeometryUtils::contextGapOverrideMap.
    return GeometryUtils::contextGapOverrideMap(
        m_layoutRegistry->resolveContextGaps(screenId, virtualDesktop, activity));
}

int DaemonGeometryResolver::resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorEngine::GeometryDefaults::ZonePadding;
    }
    return GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId, contextGapOverrideFor(screenId));
}

PhosphorLayout::EdgeGaps DaemonGeometryResolver::resolveOuterGaps(PhosphorZones::Layout* layout,
                                                                  const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    }
    return GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId, contextGapOverrideFor(screenId));
}

int DaemonGeometryResolver::defaultBorderWidth() const
{
    return m_settings ? m_settings->borderWidth() : 2;
}

int DaemonGeometryResolver::defaultBorderRadius() const
{
    return m_settings ? m_settings->borderRadius() : 0;
}

int DaemonGeometryResolver::snapBorderInset() const
{
    // No settings → no border drawn → no inset.
    if (!m_settings || !m_settings->snappingShowBorder()) {
        return 0;
    }
    // Gate on title-bar mode — this is the crux of why borderless and decorated
    // windows need OPPOSITE handling:
    //   * Title bars SHOWN (decorated): the effect draws our border on the
    //     window's DECORATION edge, which sits at the frame boundary. A window
    //     filling its zone exactly puts that border on the zone edge, so it spills
    //     past / collides with the neighbour. Inset the frame by the border width
    //     to keep the decorated window + its border inside the zone.
    //   * Title bars HIDDEN (borderless): the window is stripped to its content
    //     and fills the zone; the border is recoloured INSIDE the outermost
    //     content band, so it never extends past the window. Insetting here would
    //     just leave an empty border-width gap to the zone edge ("too small").
    if (m_settings->snappingHideTitleBars()) {
        return 0;
    }
    // Mirror the effect's snap border width exactly: the global snapping border
    // width applied to every snapped window's frame edge (snaphandler's single
    // per-mode BorderState::width, fed from snappingBorderWidth via
    // daemon_bringup.cpp). Zone custom widths only style snap-assist previews,
    // not committed frames, so they must NOT drive the inset.
    return qMax(0, m_settings->snappingBorderWidth());
}

} // namespace PlasmaZones

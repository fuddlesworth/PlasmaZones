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
    const int virtualDesktop = m_currentVirtualDesktop ? m_currentVirtualDesktop(screenId) : 0;
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

GapProvenance DaemonGeometryResolver::resolveGapProvenance(PhosphorZones::Layout* layout, const QString& screenId) const
{
    GapProvenance prov;
    const QVariantMap ruleOverride = contextGapOverrideFor(screenId);
    prov.zonePadding =
        GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId, ruleOverride, &prov.zonePaddingLayer);
    prov.outerGaps =
        GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId, ruleOverride, &prov.outerGapsLayer);
    return prov;
}

int DaemonGeometryResolver::defaultBorderWidth() const
{
    return m_settings ? m_settings->borderWidth() : 2;
}

int DaemonGeometryResolver::defaultBorderRadius() const
{
    return m_settings ? m_settings->borderRadius() : 0;
}

} // namespace PlasmaZones

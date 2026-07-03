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
    if (screenId.isEmpty()) {
        return {};
    }
    // Translation to the PerScreenSnappingKey-shaped map is shared with the
    // preview/query geometry helpers via GeometryUtils::contextGapOverrideMap.
    // This is the snap-commit geometry path, so resolve against the "snapping"
    // placement mode — a per-mode `Mode Equals "snapping"` gap rule applies here.
    // The config per-monitor gap is merged UNDER the rule override so a user gap
    // rule still wins per slot, while a monitor with only a config gap gets it.
    QVariantMap ruleGaps;
    if (m_layoutRegistry) {
        const int virtualDesktop = m_currentVirtualDesktop ? m_currentVirtualDesktop(screenId) : 0;
        const QString activity = m_currentActivity ? m_currentActivity() : QString();
        ruleGaps = GeometryUtils::contextGapOverrideMap(
            m_layoutRegistry->resolveContextGaps(screenId, virtualDesktop, activity, QStringLiteral("snapping")));
    }
    return GeometryUtils::mergeConfigPerScreenGaps(std::move(ruleGaps), m_settings, screenId);
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

} // namespace PlasmaZones

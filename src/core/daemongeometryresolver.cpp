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
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    QVariantMap map;
    if (!m_layoutRegistry || screenId.isEmpty()) {
        return map;
    }
    const int virtualDesktop = m_currentVirtualDesktop ? m_currentVirtualDesktop() : 0;
    const QString activity = m_currentActivity ? m_currentActivity() : QString();
    const PhosphorZones::ContextGapOverride gaps =
        m_layoutRegistry->resolveContextGaps(screenId, virtualDesktop, activity);
    if (gaps.isEmpty()) {
        return map;
    }
    if (gaps.zonePadding) {
        map.insert(QString(PSK::ZonePadding), *gaps.zonePadding);
    }
    if (gaps.outerGap) {
        map.insert(QString(PSK::OuterGap), *gaps.outerGap);
    }
    if (gaps.usePerSideOuterGap) {
        map.insert(QString(PSK::UsePerSideOuterGap), *gaps.usePerSideOuterGap);
    }
    if (gaps.outerGapTop) {
        map.insert(QString(PSK::OuterGapTop), *gaps.outerGapTop);
    }
    if (gaps.outerGapBottom) {
        map.insert(QString(PSK::OuterGapBottom), *gaps.outerGapBottom);
    }
    if (gaps.outerGapLeft) {
        map.insert(QString(PSK::OuterGapLeft), *gaps.outerGapLeft);
    }
    if (gaps.outerGapRight) {
        map.insert(QString(PSK::OuterGapRight), *gaps.outerGapRight);
    }
    return map;
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

} // namespace PlasmaZones

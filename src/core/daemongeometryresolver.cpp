// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemongeometryresolver.h"
#include "geometryutils.h"
#include "isettings.h"
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorLayoutApi/EdgeGaps.h>

namespace PlasmaZones {

int DaemonGeometryResolver::resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorEngine::GeometryDefaults::ZonePadding;
    }
    return GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
}

PhosphorLayout::EdgeGaps DaemonGeometryResolver::resolveOuterGaps(PhosphorZones::Layout* layout,
                                                                  const QString& screenId) const
{
    if (!m_settings) {
        return PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    }
    return GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);
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

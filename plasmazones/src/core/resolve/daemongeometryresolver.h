// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorPlacement/IGeometryResolver.h>

#include <QString>
#include <QVariantMap>

#include <functional>

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Bridges the daemon's ISettings + window-rule context resolution to the
/// placement library's IGeometryResolver. Gaps resolve through the cascade
/// context-rule override → per-screen → layout → global → default. The
/// context-rule layer is resolved for the CURRENT virtual desktop + activity
/// (supplied via callbacks so this stays free of a workspaces dependency),
/// consistent with the current-desktop occupancy filter already used at the
/// snap-geometry call sites.
class PLASMAZONES_EXPORT DaemonGeometryResolver : public PhosphorPlacement::IGeometryResolver
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

    int resolveInnerGap(PhosphorZones::Layout* layout, const QString& screenId) const override;
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const override;
    int defaultBorderWidth() const override;
    int defaultBorderRadius() const override;
    int snapBorderInset() const override;

private:
    /// Build a PerScreenKeys-shaped override map from the context rules
    /// resolved for @p screenId in the current desktop/activity. Empty when no
    /// gap rule matches (so the geometry cascade falls through to per-screen).
    QVariantMap contextGapOverrideFor(const QString& screenId) const;

    ISettings* m_settings;
    PhosphorZones::LayoutRegistry* m_layoutRegistry;
    std::function<int(const QString&)> m_currentVirtualDesktop;
    std::function<QString()> m_currentActivity;
};

} // namespace PlasmaZones

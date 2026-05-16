// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorPlacement/IGeometryResolver.h>

namespace PlasmaZones {

class ISettings;

class DaemonGeometryResolver : public PhosphorPlacement::IGeometryResolver
{
public:
    explicit DaemonGeometryResolver(ISettings* settings)
        : m_settings(settings)
    {
    }

    int resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const override;
    PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const override;
    int defaultBorderWidth() const override;
    int defaultBorderRadius() const override;

private:
    ISettings* m_settings;
};

} // namespace PlasmaZones

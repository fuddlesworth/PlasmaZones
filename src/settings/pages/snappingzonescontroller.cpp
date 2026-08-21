// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snappingzonescontroller.h"

#include "config/configdefaults.h"

#include <QString>

namespace PlasmaZones {

SnappingZonesController::SnappingZonesController(QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("snapping-zones"), parent)
{
}

int SnappingZonesController::borderWidthMin() const
{
    return ConfigDefaults::borderWidthMin();
}

int SnappingZonesController::borderWidthMax() const
{
    return ConfigDefaults::borderWidthMax();
}

int SnappingZonesController::borderRadiusMin() const
{
    return ConfigDefaults::borderRadiusMin();
}

int SnappingZonesController::borderRadiusMax() const
{
    return ConfigDefaults::borderRadiusMax();
}

double SnappingZonesController::labelFontScaleMin() const
{
    return ConfigDefaults::labelFontSizeScaleMin();
}

double SnappingZonesController::labelFontScaleMax() const
{
    return ConfigDefaults::labelFontSizeScaleMax();
}

} // namespace PlasmaZones

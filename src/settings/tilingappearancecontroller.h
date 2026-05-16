// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Tiling → Appearance" settings page.
///
/// CONSTANT bounds for the autotile border-width / border-radius sliders.
/// Live border values are on Settings (Q_PROPERTY) and bind via
/// `appSettings.autotileBorderWidth` / `appSettings.autotileBorderRadius`.
class TilingAppearanceController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int autotileBorderWidthMin READ autotileBorderWidthMin CONSTANT)
    Q_PROPERTY(int autotileBorderWidthMax READ autotileBorderWidthMax CONSTANT)
    Q_PROPERTY(int autotileBorderRadiusMin READ autotileBorderRadiusMin CONSTANT)
    Q_PROPERTY(int autotileBorderRadiusMax READ autotileBorderRadiusMax CONSTANT)

public:
    using QObject::QObject;

    int autotileBorderWidthMin() const
    {
        return ConfigDefaults::autotileBorderWidthMin();
    }
    int autotileBorderWidthMax() const
    {
        return ConfigDefaults::autotileBorderWidthMax();
    }
    int autotileBorderRadiusMin() const
    {
        return ConfigDefaults::autotileBorderRadiusMin();
    }
    int autotileBorderRadiusMax() const
    {
        return ConfigDefaults::autotileBorderRadiusMax();
    }
};

} // namespace PlasmaZones

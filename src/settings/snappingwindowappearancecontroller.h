// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorSettingsUi/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Window Appearance" settings page.
///
/// CONSTANT bounds for the snapped-window border-width / border-radius sliders
/// (parallel to TilingAppearanceController). Live border values are on Settings
/// (Q_PROPERTY) and bind via `appSettings.snapWindowBorderWidth` /
/// `appSettings.snapWindowBorderRadius`.
class SnappingWindowAppearanceController : public PhosphorSettingsUi::PageController
{
    Q_OBJECT

    Q_PROPERTY(int snapWindowBorderWidthMin READ snapWindowBorderWidthMin CONSTANT)
    Q_PROPERTY(int snapWindowBorderWidthMax READ snapWindowBorderWidthMax CONSTANT)
    Q_PROPERTY(int snapWindowBorderRadiusMin READ snapWindowBorderRadiusMin CONSTANT)
    Q_PROPERTY(int snapWindowBorderRadiusMax READ snapWindowBorderRadiusMax CONSTANT)

public:
    explicit SnappingWindowAppearanceController(QObject* parent = nullptr)
        : PhosphorSettingsUi::PageController(QStringLiteral("snapping-window-appearance"), parent)
    {
    }

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    int snapWindowBorderWidthMin() const
    {
        return ConfigDefaults::snapWindowBorderWidthMin();
    }
    int snapWindowBorderWidthMax() const
    {
        return ConfigDefaults::snapWindowBorderWidthMax();
    }
    int snapWindowBorderRadiusMin() const
    {
        return ConfigDefaults::snapWindowBorderRadiusMin();
    }
    int snapWindowBorderRadiusMax() const
    {
        return ConfigDefaults::snapWindowBorderRadiusMax();
    }
};

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Zones" settings page (the
/// drag-time zone overlay).
///
/// Exposed as a child Q_PROPERTY on SettingsController. Provides the
/// zone-overlay border and label slider bounds from ConfigDefaults.
class SnappingZonesController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int borderWidthMin READ borderWidthMin CONSTANT)
    Q_PROPERTY(int borderWidthMax READ borderWidthMax CONSTANT)
    Q_PROPERTY(int borderRadiusMin READ borderRadiusMin CONSTANT)
    Q_PROPERTY(int borderRadiusMax READ borderRadiusMax CONSTANT)
    // Zone-label font-scale slider bounds (as a 0..1+ multiplier — the QML
    // works in percent, multiplying by 100).
    Q_PROPERTY(double labelFontScaleMin READ labelFontScaleMin CONSTANT)
    Q_PROPERTY(double labelFontScaleMax READ labelFontScaleMax CONSTANT)

public:
    explicit SnappingZonesController(QObject* parent = nullptr);

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

    int borderWidthMin() const;
    int borderWidthMax() const;
    int borderRadiusMin() const;
    int borderRadiusMax() const;
    double labelFontScaleMin() const;
    double labelFontScaleMax() const;
};

} // namespace PlasmaZones

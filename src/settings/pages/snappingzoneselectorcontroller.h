// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config/configdefaults.h"

#include <PhosphorControl/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Zone Selector" settings page.
///
/// All properties are CONSTANT bounds sourced from ConfigDefaults — the
/// page's live settings (preview width/height, grid columns, trigger
/// distance, outer gap values) are already exposed on the Settings class
/// and read directly via `appSettings.X` from QML. This sub-controller
/// only owns the per-widget min/max bounds so the sliders and spin boxes
/// don't hard-code numbers in QML.
class SnappingZoneSelectorController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int triggerDistanceMin READ triggerDistanceMin CONSTANT)
    Q_PROPERTY(int triggerDistanceMax READ triggerDistanceMax CONSTANT)
    Q_PROPERTY(int previewWidthMin READ previewWidthMin CONSTANT)
    Q_PROPERTY(int previewWidthMax READ previewWidthMax CONSTANT)
    // Small/Medium/Large quick-size preset widths for the popup preview.
    Q_PROPERTY(int previewWidthSmall READ previewWidthSmall CONSTANT)
    Q_PROPERTY(int previewWidthMedium READ previewWidthMedium CONSTANT)
    Q_PROPERTY(int previewWidthLarge READ previewWidthLarge CONSTANT)
    Q_PROPERTY(int previewHeightMin READ previewHeightMin CONSTANT)
    Q_PROPERTY(int previewHeightMax READ previewHeightMax CONSTANT)
    Q_PROPERTY(int gridColumnsMin READ gridColumnsMin CONSTANT)
    Q_PROPERTY(int gridColumnsMax READ gridColumnsMax CONSTANT)
    Q_PROPERTY(int maxRowsMin READ maxRowsMin CONSTANT)
    Q_PROPERTY(int maxRowsMax READ maxRowsMax CONSTANT)

public:
    explicit SnappingZoneSelectorController(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("snapping-zoneselector"), parent)
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

    int triggerDistanceMin() const
    {
        return ConfigDefaults::triggerDistanceMin();
    }
    int triggerDistanceMax() const
    {
        return ConfigDefaults::triggerDistanceMax();
    }
    int previewWidthMin() const
    {
        return ConfigDefaults::previewWidthMin();
    }
    int previewWidthMax() const
    {
        return ConfigDefaults::previewWidthMax();
    }
    int previewWidthSmall() const
    {
        return ConfigDefaults::previewWidthSmall();
    }
    int previewWidthMedium() const
    {
        return ConfigDefaults::previewWidth();
    }
    int previewWidthLarge() const
    {
        return ConfigDefaults::previewWidthLarge();
    }
    int previewHeightMin() const
    {
        return ConfigDefaults::previewHeightMin();
    }
    int previewHeightMax() const
    {
        return ConfigDefaults::previewHeightMax();
    }
    int gridColumnsMin() const
    {
        return ConfigDefaults::gridColumnsMin();
    }
    int gridColumnsMax() const
    {
        return ConfigDefaults::gridColumnsMax();
    }
    int maxRowsMin() const
    {
        return ConfigDefaults::maxRowsMin();
    }
    int maxRowsMax() const
    {
        return ConfigDefaults::maxRowsMax();
    }
};

} // namespace PlasmaZones

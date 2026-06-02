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
    // Snapping gaps (zone padding + outer/edge gaps) moved onto Window →
    // Appearance (per-screen, beside the global border/title-bar cards). Each
    // field is bounded by the SAME ConfigDefaults accessor the per-screen
    // validator clamps against (zonePadding* for the padding spin box, outerGap*
    // for every edge/per-side gap) so the UI range and the clamp range can never
    // drift apart.
    Q_PROPERTY(int zonePaddingMin READ zonePaddingMin CONSTANT)
    Q_PROPERTY(int zonePaddingMax READ zonePaddingMax CONSTANT)
    Q_PROPERTY(int gapMin READ gapMin CONSTANT)
    Q_PROPERTY(int gapMax READ gapMax CONSTANT)

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
    int zonePaddingMin() const
    {
        return ConfigDefaults::zonePaddingMin();
    }
    int zonePaddingMax() const
    {
        return ConfigDefaults::zonePaddingMax();
    }
    int gapMin() const
    {
        return ConfigDefaults::outerGapMin();
    }
    int gapMax() const
    {
        return ConfigDefaults::outerGapMax();
    }
};

} // namespace PlasmaZones

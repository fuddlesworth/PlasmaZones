// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorControl/PageController.h>
#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Tiling → Appearance" settings page.
///
/// CONSTANT bounds for the autotile border-width / border-radius sliders.
/// Live border values are on Settings (Q_PROPERTY) and bind via
/// `appSettings.autotileBorderWidth` / `appSettings.autotileBorderRadius`.
class TilingAppearanceController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(int autotileBorderWidthMin READ autotileBorderWidthMin CONSTANT)
    Q_PROPERTY(int autotileBorderWidthMax READ autotileBorderWidthMax CONSTANT)
    Q_PROPERTY(int autotileBorderRadiusMin READ autotileBorderRadiusMin CONSTANT)
    Q_PROPERTY(int autotileBorderRadiusMax READ autotileBorderRadiusMax CONSTANT)
    // Gaps moved onto Window → Appearance (per-screen, beside the global
    // border/title-bar settings). Inner and outer/per-side gaps are bounded by
    // their own ConfigDefaults accessors — the same ones the per-screen autotile
    // validator clamps against — so each spin box's UI range matches its clamp
    // range exactly (they resolve to the shared AutotileDefaults range today,
    // but stay coupled if either is ever retuned independently).
    Q_PROPERTY(int autotileInnerGapMin READ autotileInnerGapMin CONSTANT)
    Q_PROPERTY(int autotileInnerGapMax READ autotileInnerGapMax CONSTANT)
    Q_PROPERTY(int autotileGapMin READ autotileGapMin CONSTANT)
    Q_PROPERTY(int autotileGapMax READ autotileGapMax CONSTANT)

public:
    explicit TilingAppearanceController(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("tiling-appearance"), parent)
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
    int autotileInnerGapMin() const
    {
        return ConfigDefaults::autotileInnerGapMin();
    }
    int autotileInnerGapMax() const
    {
        return ConfigDefaults::autotileInnerGapMax();
    }
    int autotileGapMin() const
    {
        return ConfigDefaults::autotileOuterGapMin();
    }
    int autotileGapMax() const
    {
        return ConfigDefaults::autotileOuterGapMax();
    }
};

} // namespace PlasmaZones

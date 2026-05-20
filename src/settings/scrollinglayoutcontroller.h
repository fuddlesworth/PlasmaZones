// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <QObject>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Scrolling → Layout" and
/// "Scrolling → Appearance" settings pages.
///
/// Pure CONSTANT-bounds facade over ConfigDefaults so the QML side can
/// bind SpinBox / Slider `from:` and `to:` to the same numeric clamps the
/// Settings backend enforces on save — keeping the UI and the schema in
/// lockstep without hand-synced literals.
///
/// Mirrors @ref TilingAppearanceController and @ref TilingAlgorithmController:
/// no Settings wiring is required because the live values are already
/// Q_PROPERTY on Settings and bind through `appSettings.scrollX`.
class ScrollingLayoutController : public QObject
{
    Q_OBJECT

    // Layout-page bounds
    Q_PROPERTY(int scrollInnerGapMin READ scrollInnerGapMin CONSTANT)
    Q_PROPERTY(int scrollInnerGapMax READ scrollInnerGapMax CONSTANT)
    Q_PROPERTY(int scrollOuterGapMin READ scrollOuterGapMin CONSTANT)
    Q_PROPERTY(int scrollOuterGapMax READ scrollOuterGapMax CONSTANT)
    Q_PROPERTY(double scrollColumnWidthMin READ scrollColumnWidthMin CONSTANT)
    Q_PROPERTY(double scrollColumnWidthMax READ scrollColumnWidthMax CONSTANT)
    Q_PROPERTY(double scrollDefaultColumnWidth READ scrollDefaultColumnWidth CONSTANT)

    // Appearance-page bounds (column borders + corner radius)
    Q_PROPERTY(int scrollBorderWidthMin READ scrollBorderWidthMin CONSTANT)
    Q_PROPERTY(int scrollBorderWidthMax READ scrollBorderWidthMax CONSTANT)
    Q_PROPERTY(int scrollBorderRadiusMin READ scrollBorderRadiusMin CONSTANT)
    Q_PROPERTY(int scrollBorderRadiusMax READ scrollBorderRadiusMax CONSTANT)

public:
    using QObject::QObject;

    int scrollInnerGapMin() const
    {
        return ConfigDefaults::scrollInnerGapMin();
    }
    int scrollInnerGapMax() const
    {
        return ConfigDefaults::scrollInnerGapMax();
    }
    int scrollOuterGapMin() const
    {
        return ConfigDefaults::scrollOuterGapMin();
    }
    int scrollOuterGapMax() const
    {
        return ConfigDefaults::scrollOuterGapMax();
    }
    double scrollColumnWidthMin() const
    {
        return ConfigDefaults::scrollColumnWidthMin();
    }
    double scrollColumnWidthMax() const
    {
        return ConfigDefaults::scrollColumnWidthMax();
    }
    double scrollDefaultColumnWidth() const
    {
        return ConfigDefaults::scrollDefaultColumnWidth();
    }
    int scrollBorderWidthMin() const
    {
        return ConfigDefaults::scrollBorderWidthMin();
    }
    int scrollBorderWidthMax() const
    {
        return ConfigDefaults::scrollBorderWidthMax();
    }
    int scrollBorderRadiusMin() const
    {
        return ConfigDefaults::scrollBorderRadiusMin();
    }
    int scrollBorderRadiusMax() const
    {
        return ConfigDefaults::scrollBorderRadiusMax();
    }
};

} // namespace PlasmaZones

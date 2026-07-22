// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the "Snapping → Zones" settings page (the
/// drag-time zone overlay).
///
/// Exposed as a child Q_PROPERTY on SettingsController. Two responsibilities:
///   1. Expose the zone-overlay border-width / border-radius slider bounds as
///      CONSTANTs, delegating directly to ConfigDefaults::borderWidthMin/Max and
///      borderRadiusMin/Max.
///   2. Own the color-import action surface: `loadColorsFromPywal()` and
///      `loadColorsFromFile(path)`, plus their `colorImportError(msg)` /
///      `colorImportSuccess()` signals. Each successful import emits
///      `changed()` so SettingsController's dirty tracking flips to the
///      appropriate page — the live color properties themselves are
///      Q_PROPERTY on Settings and mark dirty through the meta-loop, but
///      the top-level load path needs the explicit signal to cover any
///      batched writes that don't individually trip a NOTIFY.
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
    explicit SnappingZonesController(ISettings& settings, QObject* parent = nullptr);

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

    /// Import colors from the user's pywal output
    /// (`~/.cache/wal/colors.json`). Emits `colorImportSuccess()` on
    /// success or `colorImportError(msg)` if the file is missing or
    /// unparseable.
    Q_INVOKABLE void loadColorsFromPywal();

    /// Import colors from an arbitrary colors.json file. See
    /// `loadColorsFromPywal` for signal semantics.
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);

Q_SIGNALS:
    void colorImportError(const QString& error);
    void colorImportSuccess();

    /// Generic "something changed" — SettingsController hooks this to
    /// `onSettingsPropertyChanged()` so successful imports mark the page
    /// dirty even if the underlying Settings property fan-out didn't
    /// individually trip a NOTIFY.
    void changed();

private:
    ISettings* m_settings = nullptr;
};

} // namespace PlasmaZones

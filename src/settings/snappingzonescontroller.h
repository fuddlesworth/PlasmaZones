// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QColor>
#include <QObject>
#include <QString>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Zones" settings page (the
/// drag-time zone overlay).
///
/// Exposed as a child Q_PROPERTY on SettingsController. Two responsibilities:
///   1. Expose the zone-overlay border-width / border-radius slider bounds as
///      CONSTANTs, delegating directly to ConfigDefaults::borderWidthMin/Max and
///      borderRadiusMin/Max.
///   2. Own the color-import action surface: `loadColorsFromPywal()` and
///      `loadColorsFromFile(path)`, plus their `colorImportError(msg)` /
///      `colorImportSuccess()` signals. Since v5 the overlay colours are
///      rule-backed, so the controller only PARSES the file (ColorImporter)
///      and hands the extracted colours to the Overlay Appearance page via
///      `colorsImported(...)` — the page writes them onto the managed
///      baseline overlay rule through the RuleController (staged like any
///      other overlay edit), sets the config-backed label colour, and turns
///      the system-colours gate off.
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

    /// Parsed colours from a successful import. The Overlay Appearance page
    /// applies them: the three overlay colours go onto the managed baseline
    /// overlay rule (RuleController), the label colour onto the config-backed
    /// labelFontColor, and useSystemColors switches off so the imported
    /// colours are actually visible.
    void colorsImported(const QColor& highlight, const QColor& inactive, const QColor& border, const QColor& labelFont);
    // The old generic `changed()` signal retired with the v5 import rework:
    // every applied value now flows through a tracked path (rule-model writes
    // or Q_PROPERTY NOTIFYs), so there is no batched write left for it to cover.
};

} // namespace PlasmaZones

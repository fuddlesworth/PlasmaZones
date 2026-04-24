// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace PlasmaZones {

class Settings;

/// Q_PROPERTY surface for the "Snapping → Appearance" settings page.
///
/// Exposed as a child Q_PROPERTY on SettingsController. Two responsibilities:
///   1. Expose border-width / border-radius slider bounds as CONSTANTs
///      (ConfigDefaults delegation, mirrors SnappingZoneSelectorController).
///   2. Own the color-import action surface: `loadColorsFromPywal()` and
///      `loadColorsFromFile(path)`, plus their `colorImportError(msg)` /
///      `colorImportSuccess()` signals. Each successful import emits
///      `changed()` so SettingsController's dirty tracking flips to the
///      appropriate page — the live color properties themselves are
///      Q_PROPERTY on Settings and mark dirty through the meta-loop, but
///      the top-level load path needs the explicit signal to cover any
///      batched writes that don't individually trip a NOTIFY.
class SnappingAppearanceController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int borderWidthMin READ borderWidthMin CONSTANT)
    Q_PROPERTY(int borderWidthMax READ borderWidthMax CONSTANT)
    Q_PROPERTY(int borderRadiusMin READ borderRadiusMin CONSTANT)
    Q_PROPERTY(int borderRadiusMax READ borderRadiusMax CONSTANT)

public:
    explicit SnappingAppearanceController(Settings* settings, QObject* parent = nullptr);

    int borderWidthMin() const;
    int borderWidthMax() const;
    int borderRadiusMin() const;
    int borderRadiusMax() const;

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
    Settings* m_settings = nullptr;
};

} // namespace PlasmaZones

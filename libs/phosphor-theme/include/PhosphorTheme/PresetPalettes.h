// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorTheme {

// Hand-curated contrasting palettes used to demonstrate live retinting
// without depending on matugen or an on-disk JSON.
//
// Each preset is a fully populated token map (all M3 + ANSI 16 + brand
// stops) so applying one through `PaletteStore::applyTokens` retints
// every bound surface in one shot — same code path the matugen runner
// uses when a wallpaper changes.
//
// The list is intentionally small (dark / light / sunset / forest) — this
// is a demonstration aid, not a theme distribution channel. Real themes
// ship as JSON under `~/.local/share/phosphor/palettes/` and load through
// PaletteStore directly.
class PHOSPHORTHEME_EXPORT PresetPalettes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit PresetPalettes(QObject* parent = nullptr);

    // Canonical Phosphor dark — same values as PaletteStore's built-in
    // default. Exposed here too so the demo's "Dark" preset button has
    // somewhere to bind without round-tripping through resetToDefaults.
    [[nodiscard]] Q_INVOKABLE QVariantMap dark() const;

    // Light variant — inverted surfaces, deeper accent colors for legible
    // contrast on a white background.
    [[nodiscard]] Q_INVOKABLE QVariantMap light() const;

    // Warm sunset palette — orange / coral / pink against a deep maroon.
    [[nodiscard]] Q_INVOKABLE QVariantMap sunset() const;

    // Cool forest palette — green / lime / cyan against deep moss.
    [[nodiscard]] Q_INVOKABLE QVariantMap forest() const;

    // Preset name list in display order. Use with `byName` to drive a
    // round-robin cycle or a chooser UI without hardcoding the four
    // method names everywhere.
    [[nodiscard]] Q_INVOKABLE QStringList names() const;

    // Look up a preset by name; returns an empty map for unknown names
    // so applyTokens treats it as a no-op rather than crashing.
    [[nodiscard]] Q_INVOKABLE QVariantMap byName(const QString& name) const;
};

} // namespace PhosphorTheme

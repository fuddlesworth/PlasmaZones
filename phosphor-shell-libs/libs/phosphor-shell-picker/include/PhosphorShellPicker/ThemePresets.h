// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorShellPicker {

/**
 * @brief The theme tiles of the picker strip.
 *
 * Two are built in: Dark (PaletteStore's canonical palette) and Light
 * (the same spectrum on a pale ground, per the light variant in the
 * picker mock). Any palette JSON in `directory` follows, one tile per
 * file, named by its base name; `current.json` is skipped because it is
 * the shell's own committed palette, not a preset. Files use the
 * `{ "tokens": { ... } }` shape PaletteStore reads, a flat object is
 * accepted too.
 *
 * Each preset is `{ name, tokens, swatches }`: `swatches` is the
 * five-colour strip a tile draws (surface, container, primary,
 * secondary, tertiary), `tokens` is what the retint applies.
 */
class ThemePresets : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// Where user palettes live. Defaults to `defaultDirectory()`.
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(int count READ count NOTIFY presetsChanged)

public:
    explicit ThemePresets(QObject* parent = nullptr);
    ~ThemePresets() override;

    [[nodiscard]] QString directory() const;
    void setDirectory(const QString& directory);

    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] int count() const;

    /// `<AppLocalDataLocation>/palettes`, beside the committed palette.
    [[nodiscard]] static QString defaultDirectory();

    /// The canonical dark palette.
    [[nodiscard]] static QVariantMap darkPalette();
    /// The light variant: sky, blue, violet, rose on `#F6F9FF`.
    [[nodiscard]] static QVariantMap lightPalette();

    /// The five swatches a tile shows for `tokens`.
    [[nodiscard]] static QVariantList swatchesFor(const QVariantMap& tokens);

    /// Parse one palette file into a token map (empty on failure).
    [[nodiscard]] static QVariantMap readPaletteFile(const QString& path);

    /// Rebuild the list: the two built-ins, then the directory's files
    /// by name. Emits presetsChanged only when something differs.
    Q_INVOKABLE void rescan();

    /// The tokens of the preset called `name`, or an empty map.
    [[nodiscard]] Q_INVOKABLE QVariantMap tokensFor(const QString& name) const;

Q_SIGNALS:
    void directoryChanged();
    void presetsChanged();

private:
    QString m_directory;
    QVariantList m_presets;
};

} // namespace PhosphorShellPicker

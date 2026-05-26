// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/phosphortheme_export.h>

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QFileSystemWatcher;

namespace PhosphorTheme {

// Concrete IThemeService backed by an in-memory QVariantMap. JSON files are
// loaded eagerly and watched for hot-reload; the file's `tokens` object
// (token name → "#RRGGBB" or "#AARRGGBB") replaces the active palette in
// place and `paletteChanged` fires once per reload.
//
// QML side: registered as a QML singleton via `QML_ELEMENT QML_SINGLETON`,
// imported as `import Phosphor.Theme` → `PaletteStore.token("primary")`.
// QML files in Phosphor.Theme (Theme.qml, etc.) wrap PaletteStore in
// nicer property aliases so consumers write `Theme.primary` instead of
// `PaletteStore.token("primary")`.
//
// This is a per-engine instance, not a process global. The engine owns
// the lifetime; alternate implementations swap in via
// `qmlRegisterSingletonInstance` before module import.
class PHOSPHORTHEME_EXPORT PaletteStore : public QObject, public IThemeService
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // QML-friendly accessor exposing the full token map. Bindings on
    // `palette` re-evaluate when the active palette changes.
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY paletteChanged)

    // The currently-watched JSON path, or empty if the active palette
    // came from `loadFromJson` / defaults.
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourcePathChanged)

public:
    explicit PaletteStore(QObject* parent = nullptr);
    ~PaletteStore() override;

    // IThemeService.
    [[nodiscard]] QVariantMap palette() const override;
    [[nodiscard]] Q_INVOKABLE QColor token(const QString& name) const override;
    Q_INVOKABLE bool loadFromJson(const QByteArray& json) override;
    Q_INVOKABLE bool loadFromFile(const QString& path) override;
    Q_INVOKABLE void applyTokens(const QVariantMap& tokens) override;
    Q_INVOKABLE void resetToDefaults() override;

    [[nodiscard]] QString sourcePath() const;

Q_SIGNALS:
    void paletteChanged();
    void sourcePathChanged();

    // Fires when `loadFromFile` failed to parse or read the file. The
    // active palette is unchanged. Wired to the demo's status bar so a
    // bad save is visible without crashing the shell.
    void loadError(const QString& path, const QString& reason);

private:
    void applyPalette(const QVariantMap& tokens);
    void reloadFromCurrentPath();

    QVariantMap m_palette;
    QString m_sourcePath;
    std::unique_ptr<QFileSystemWatcher> m_watcher;
};

} // namespace PhosphorTheme

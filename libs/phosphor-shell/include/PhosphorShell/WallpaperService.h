// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorShaders {
class IWallpaperProvider;
}

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorShell {

/**
 * @brief Asynchronous wallpaper-image source for shader backgrounds.
 *
 * Wraps `PhosphorShaders::createWallpaperProvider()` to resolve the
 * current desktop wallpaper path (Plasma / Hyprland / Sway / GNOME)
 * and decodes the image on a worker thread via QThreadPool — a 4 K
 * PNG decode would otherwise visibly stutter the shell.
 *
 * The Plasma desktop-applets config file is watched when present so
 * wallpaper changes from System Settings propagate automatically.
 * Non-KDE desktops can call `refresh()` manually after writing a
 * new wallpaper (e.g. through xdg-desktop-portal).
 *
 * Usage from QML via `PhosphorShell.wallpaper`:
 *
 *     ShaderBackground {
 *         useWallpaper: PhosphorShell.wallpaper.available
 *         wallpaperTexture: PhosphorShell.wallpaper.image
 *     }
 *
 * Threading: every public method MUST be called from the GUI thread.
 * The worker thread that decodes the image marshals back to the GUI
 * thread via QMetaObject::invokeMethod, so consumers never see the
 * worker's QImage directly.
 */
class PHOSPHORSHELL_EXPORT WallpaperService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WallpaperService)

    Q_PROPERTY(QImage image READ image NOTIFY imageChanged)
    Q_PROPERTY(QString path READ path NOTIFY imageChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY imageChanged)

public:
    explicit WallpaperService(QObject* parent = nullptr);
    ~WallpaperService() override;

    [[nodiscard]] QImage image() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] bool isAvailable() const;

    /// Re-query the provider and reload the wallpaper image if the
    /// path has changed. Called automatically when the Plasma config
    /// file changes; QML can call manually to force a re-check.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void imageChanged();

private:
    void scheduleLoad(const QString& path);
    void installImage(QImage image, const QString& path);

    std::unique_ptr<PhosphorShaders::IWallpaperProvider> m_provider;
    QString m_currentPath;
    QImage m_image;
    QFileSystemWatcher* m_watcher = nullptr;
    // Generation counter discards out-of-order async-load results: a
    // rapid wallpaper-change sequence could queue multiple decode
    // tasks; only the most recently scheduled one's result is
    // installed. Incremented on every scheduleLoad.
    quint64 m_loadGeneration = 0;
};

} // namespace PhosphorShell

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorShaders {
class IWallpaperProvider;
}

namespace PhosphorShell {

/**
 * @brief The shell's own wallpaper: which image each output shows.
 *
 * The shell owns the wallpaper surface, so this service is the store the
 * surface reads and the picker writes. It is not a view onto another
 * desktop's configuration. The store is a per-screen map keyed by output
 * name, where the empty key is the wallpaper every output without an
 * entry of its own shows, persisted at `storePath()`
 * (`$XDG_CONFIG_HOME/phosphor-shell/wallpaper.json`, beside the shell
 * file the loader discovers).
 *
 * Two layers sit on top of the configured map, both per screen and both
 * keyed the same way:
 *
 *  - the configured path: what `setPath()` wrote and what survives a
 *    restart;
 *  - a transient preview: `setPreview()` / `clearPreview()`, which the
 *    picker uses to paint a hovered candidate under the windows without
 *    committing it. Never persisted.
 *
 * `effectivePath(screen)` is what a surface on that output should draw:
 * the preview if one is set (for that screen, else for all screens),
 * otherwise the configured path (for that screen, else for all screens).
 * `effectivePathChanged(screen)` fires whenever that answer may have
 * changed for `screen`, with the empty name meaning every output.
 *
 * First run: when the store is empty the service seeds it ONCE from the
 * desktop the user is coming from (through `PhosphorShaders::
 * createWallpaperProvider()`, which reads Plasma / Hyprland / sway / GNOME
 * configuration), so an existing session does not go black. After that
 * seed the store is authoritative and the other desktop's configuration
 * is never consulted again.
 *
 * `image` is the decoded `path` (the shell-wide wallpaper), decoded on a
 * worker thread via QThreadPool because a 4 K PNG decode would visibly
 * stutter the shell, and pre-converted to RGBA8888 for ShaderBackground:
 *
 *     ShaderBackground {
 *         useWallpaper: PhosphorShell.wallpaper.available
 *         wallpaperTexture: PhosphorShell.wallpaper.image
 *     }
 *
 * Threading: every public method MUST be called from the GUI thread.
 * The decode worker marshals back via QMetaObject::invokeMethod, so
 * consumers never see the worker's QImage directly.
 */
class PHOSPHORSHELL_EXPORT WallpaperService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WallpaperService)

    Q_PROPERTY(QImage image READ image NOTIFY imageChanged)
    /// The shell-wide configured wallpaper: the all-screens entry, or
    /// when only per-screen entries exist, the first of those by name.
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY imageChanged)

public:
    explicit WallpaperService(QObject* parent = nullptr);
    /// Same, with the first-run seed source injected. Tests hand in a
    /// fake so the seed can be asserted without a desktop; the default
    /// constructor uses `PhosphorShaders::createWallpaperProvider()`.
    /// Null means no seed source.
    WallpaperService(std::unique_ptr<PhosphorShaders::IWallpaperProvider> seedProvider, QObject* parent = nullptr);
    ~WallpaperService() override;

    /// Where the store lives: `$XDG_CONFIG_HOME/phosphor-shell/wallpaper.json`.
    [[nodiscard]] static QString storePath();

    [[nodiscard]] QImage image() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] bool isAvailable() const;

    /// The persisted wallpaper for `screenName`: its own entry, else the
    /// all-screens entry, else empty. The empty name reads the
    /// all-screens entry itself.
    Q_INVOKABLE [[nodiscard]] QString configuredPath(const QString& screenName = QString()) const;
    /// The transient preview for `screenName`, resolved the same way.
    /// Empty when nothing is being previewed there.
    Q_INVOKABLE [[nodiscard]] QString previewPath(const QString& screenName = QString()) const;
    /// What a surface on `screenName` should draw: the preview when one
    /// is set, otherwise the configured path.
    Q_INVOKABLE [[nodiscard]] QString effectivePath(const QString& screenName = QString()) const;

    /// Persist `path` as the wallpaper for `screenName`, or for every
    /// screen when the name is empty (which also drops the per-screen
    /// entries, since "all screens" means all of them). Returns false,
    /// and changes nothing, when `path` is not a readable image.
    Q_INVOKABLE bool setPath(const QString& path, const QString& screenName = QString());

    /// Show `path` on `screenName` (every screen when empty) without
    /// persisting it. Returns false when `path` is not a readable image.
    Q_INVOKABLE bool setPreview(const QString& path, const QString& screenName = QString());
    /// Drop the preview for `screenName`, or every preview when the name
    /// is empty.
    Q_INVOKABLE void clearPreview(const QString& screenName = QString());

    /// Re-decode `path` into `image`, for a consumer that knows the file
    /// changed on disk under the same name.
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void imageChanged();
    void pathChanged();
    /// The answer to `effectivePath(screenName)` may have changed. An
    /// empty name means every screen.
    void effectivePathChanged(const QString& screenName);

private:
    void load();
    void save() const;
    void seedFromDesktop();
    void scheduleLoad(const QString& path);
    void installImage(QImage image, const QString& path);

    // QImageReader::setAllocationLimit unit is MiB. 512 MiB covers a
    // ~16K × 16K RGBA8888 wallpaper while rejecting decompression
    // bombs.
    static constexpr int kMaxImageBytesMib = 512;
    // Pixel-count cap: reject before decode runs, so a small-header
    // image with huge declared dimensions can't make it to allocation.
    static constexpr qint64 kMaxPixelCount = qint64(16384) * 16384;

    std::unique_ptr<PhosphorShaders::IWallpaperProvider> m_seedProvider;
    // Output name → absolute path. The empty key is the all-screens entry.
    QHash<QString, QString> m_configured;
    QHash<QString, QString> m_preview;
    // The path `m_image` was decoded from.
    QString m_imagePath;
    QImage m_image;
    // Generation counter discards out-of-order async-load results: a
    // rapid wallpaper-change sequence could queue multiple decode
    // tasks; only the most recently scheduled one's result is
    // installed. Incremented on every scheduleLoad.
    quint64 m_loadGeneration = 0;
};

} // namespace PhosphorShell

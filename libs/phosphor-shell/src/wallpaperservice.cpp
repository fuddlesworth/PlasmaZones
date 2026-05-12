// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/WallpaperService.h>

#include <PhosphorShaders/IWallpaperProvider.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>

Q_LOGGING_CATEGORY(lcWallpaperService, "phosphorshell.wallpaper")

namespace PhosphorShell {

WallpaperService::WallpaperService(QObject* parent)
    : QObject(parent)
    , m_provider(PhosphorShaders::createWallpaperProvider())
{
    // Debounce the config-watcher: KDE writes the file via temp-file
    // + rename, which can fire `fileChanged` several times during a
    // single save (delete → new inode → metadata sync). Without
    // debouncing, each fire schedules a fresh `QImageReader` decode
    // on the threadpool — wasted work because the generation counter
    // discards stale results but the decode still runs.
    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(kRefreshDebounceMs);
    connect(m_refreshDebounce, &QTimer::timeout, this, &WallpaperService::refresh);

    // Watch the Plasma desktop-appletsrc config so a wallpaper change
    // via System Settings propagates automatically. Best-effort —
    // non-KDE desktops or a missing config just disable auto-refresh,
    // and the user can still call refresh() manually.
    const QString plasmaConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-org.kde.plasma.desktop-appletsrc");
    if (QFileInfo::exists(plasmaConfig)) {
        m_watcher = new QFileSystemWatcher(this);
        m_watcher->addPath(plasmaConfig);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            // KDE saves the config via atomic-rename, invalidating the
            // watch on the original inode. Re-add and (re)start the
            // debounce so a burst of fileChanged events collapses to
            // one refresh().
            if (m_watcher && !m_watcher->files().contains(path) && QFileInfo::exists(path)) {
                m_watcher->addPath(path);
            }
            m_refreshDebounce->start();
        });
    }
    refresh();
}

WallpaperService::~WallpaperService() = default;

QImage WallpaperService::image() const
{
    return m_image;
}

QString WallpaperService::path() const
{
    return m_currentPath;
}

bool WallpaperService::isAvailable() const
{
    return !m_image.isNull();
}

void WallpaperService::refresh()
{
    if (!m_provider) {
        return;
    }
    const QString newPath = m_provider->wallpaperPath();
    if (newPath.isEmpty()) {
        qCDebug(lcWallpaperService) << "Provider returned no wallpaper path";
        return;
    }
    if (newPath == m_currentPath && !m_image.isNull()) {
        // No-op — same path, image already loaded.
        return;
    }
    scheduleLoad(newPath);
}

void WallpaperService::scheduleLoad(const QString& path)
{
    const quint64 generation = ++m_loadGeneration;
    QPointer<WallpaperService> self(this);
    // QThreadPool::globalInstance is process-wide; one-shot image
    // decodes per wallpaper change don't justify a dedicated pool.
    // QImageReader handles auto-rotation (EXIF) and incremental
    // decode of large images more efficiently than QImage's ctor.
    QThreadPool::globalInstance()->start([self, generation, path]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // Cap memory: a pathological wallpaper (multi-GB or a
        // decompression bomb) decoded full-resolution can OOM the
        // process. 512 MiB is generous for a 16K × 16K RGBA8888
        // wallpaper (1 GB allocation) but rejects the truly absurd.
        reader.setAllocationLimit(kMaxImageBytesMib);
        // Sanity-check pixel dimensions up front so a "valid" small
        // header pointing at a huge image still bails before decode.
        const QSize probedSize = reader.size();
        if (probedSize.isValid()) {
            const qint64 pixels = qint64(probedSize.width()) * qint64(probedSize.height());
            if (pixels > kMaxPixelCount) {
                qCWarning(lcWallpaperService)
                    << "Rejecting wallpaper at" << path << "— pixel count" << pixels << "exceeds" << kMaxPixelCount;
                return;
            }
        }
        QImage img = reader.read();
        if (!img.isNull()) {
            // Pre-convert to RGBA8888 — ShaderEffect's upload path
            // requires this format, and converting on the worker
            // thread keeps the cost off the GUI thread.
            img = img.convertToFormat(QImage::Format_RGBA8888);
        }
        // Marshal back to the GUI thread. Post to qApp (always alive
        // when there's a process) and re-check QPointer inside the
        // lambda — if `self` was destroyed between scheduling and
        // dispatch, the lambda bails before any dereference.
        QMetaObject::invokeMethod(
            qApp,
            [self, generation, img, path]() {
                if (!self || generation != self->m_loadGeneration) {
                    return;
                }
                self->installImage(img, path);
            },
            Qt::QueuedConnection);
    });
}

void WallpaperService::installImage(QImage image, const QString& path)
{
    if (image.isNull()) {
        qCWarning(lcWallpaperService) << "Failed to load wallpaper at" << path;
        return;
    }
    // Skip the imageChanged emission when the path is unchanged and
    // the image cacheKey matches — i.e. nothing observable changed.
    // CLAUDE.md mandates "only emit signals when value actually
    // changes"; without this guard a series of `fileChanged` events
    // on the Plasma config that don't actually flip the wallpaper
    // line still trigger imageChanged → ShaderBackground re-uploads
    // the same texture to the GPU.
    if (path == m_currentPath && !m_image.isNull() && image.cacheKey() == m_image.cacheKey()) {
        return;
    }
    m_image = image;
    m_currentPath = path;
    qCDebug(lcWallpaperService) << "Loaded wallpaper" << path << image.size();
    Q_EMIT imageChanged();
}

} // namespace PhosphorShell

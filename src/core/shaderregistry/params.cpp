// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wallpaper resolution and overlay-specific helpers.
// Part of ShaderRegistry — split from shaderregistry.cpp for SRP.
// Parameter validation, coercion, and uniform translation are now in
// BaseShaderRegistry (baseshaderregistry.cpp).

#include "../shaderregistry.h"
#include "../logging.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMutexLocker>

namespace PlasmaZones {

// ===============================================================================
// Plasma Wallpaper Path Resolution
// ===============================================================================

std::unique_ptr<IWallpaperProvider> ShaderRegistry::s_wallpaperProvider;
QString ShaderRegistry::s_cachedWallpaperPath;
QImage ShaderRegistry::s_cachedWallpaperImage;
qint64 ShaderRegistry::s_cachedWallpaperMtime = 0;
QMutex ShaderRegistry::s_wallpaperCacheMutex;

QString ShaderRegistry::wallpaperPath()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);

    if (!s_cachedWallpaperPath.isEmpty() && QFile::exists(s_cachedWallpaperPath)) {
        return s_cachedWallpaperPath;
    }

    if (!s_wallpaperProvider) {
        s_wallpaperProvider = createWallpaperProvider();
    }

    s_cachedWallpaperPath = s_wallpaperProvider->wallpaperPath();
    return s_cachedWallpaperPath;
}

QImage ShaderRegistry::loadWallpaperImage()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);

    // Inline path resolution (avoid calling wallpaperPath which also locks)
    QString path = s_cachedWallpaperPath;
    if (path.isEmpty() || !QFile::exists(path)) {
        lock.unlock();
        wallpaperPath(); // populates s_cachedWallpaperPath (acquires lock internally)
        lock.relock();
        path = s_cachedWallpaperPath; // re-read after relock
    }
    if (path.isEmpty()) {
        return {};
    }
    // Check if cached image is still valid (same path + same mtime)
    const QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    if (!s_cachedWallpaperImage.isNull() && s_cachedWallpaperMtime == mtime) {
        return s_cachedWallpaperImage;
    }
    QImage img(path);
    if (img.isNull()) {
        return {};
    }
    s_cachedWallpaperImage = img.convertToFormat(QImage::Format_RGBA8888);
    s_cachedWallpaperMtime = mtime;
    qCDebug(lcCore) << "Loaded and cached wallpaper image:" << path << s_cachedWallpaperImage.size();
    return s_cachedWallpaperImage;
}

void ShaderRegistry::invalidateWallpaperCache()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);
    s_cachedWallpaperPath.clear();
    s_cachedWallpaperImage = QImage();
    s_cachedWallpaperMtime = 0;
    s_wallpaperProvider.reset(); // force re-detection on next call
}

} // namespace PlasmaZones

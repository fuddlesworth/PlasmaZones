// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ShaderRegistry's wallpaper-path resolution and its process-wide caches: the
// provider handle, the decoded-image cache, and the fixed-capacity crop cache.
//
// Split out of shaderregistry.cpp, which was past the 1150-line hard ceiling.
// This is the natural seam: the block owns its own statics and its own mutex,
// nothing else in the registry touches them, and the registry's pack-scanning
// half has no wallpaper concept at all. Same class, separate TU, no API change.

#include <PhosphorShaders/ShaderRegistry.h>

#include <PhosphorShaders/IWallpaperProvider.h>

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutexLocker>
#include <QtGui/QImage>

namespace PhosphorShaders {

// Defined in shaderregistry.cpp — the same category, so wallpaper diagnostics
// stay filterable with the rest of the registry's rather than needing their own
// rule. Declared rather than redefined: two Q_LOGGING_CATEGORY definitions of
// one name would be an ODR violation.
Q_DECLARE_LOGGING_CATEGORY(lcShaderRegistry)

// ═══════════════════════════════════════════════════════════════════════════════
// Wallpaper Path Resolution
// ═══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<IWallpaperProvider> ShaderRegistry::s_wallpaperProvider;
QString ShaderRegistry::s_cachedWallpaperPath;
bool ShaderRegistry::s_wallpaperPathResolved = false;
qint64 ShaderRegistry::s_wallpaperPathResolvedAtMs = 0;
QImage ShaderRegistry::s_cachedWallpaperImage;
qint64 ShaderRegistry::s_cachedWallpaperMtime = 0;
QMutex ShaderRegistry::s_wallpaperCacheMutex;
std::array<ShaderRegistry::WallpaperCropEntry, ShaderRegistry::CropCacheCapacity>
    ShaderRegistry::s_cachedWallpaperCrops;
int ShaderRegistry::s_cachedWallpaperCropNextSlot = 0;

QString ShaderRegistry::wallpaperPath()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);

    if (!s_cachedWallpaperPath.isEmpty() && QFile::exists(s_cachedWallpaperPath)) {
        return s_cachedWallpaperPath;
    }

    // A NEGATIVE answer is cached too, but only for a bounded window — never
    // latched for the process lifetime.
    //
    // Why cache it at all: without this, an empty result (unsupported desktop, a
    // Plasma slideshow containment with no Image= key, swww not running) never
    // engages the fast path above, so every caller re-queries the provider — and
    // the Hyprland/sway provider spawns a QProcess and blocks in
    // waitForFinished(1000) while holding this mutex, once per overlay show and
    // once per shader attach.
    //
    // Why it must EXPIRE rather than latch: `invalidateWallpaperCache()` has no
    // callers, so there is no event that would clear it. A permanent latch means a
    // resolve that happens before plasmashell has written its config (or before
    // swww is up) leaves every wallpaper-using shader on the transparent fallback
    // for the whole session with no recovery — and before this negative cache
    // existed, that case self-healed because an empty path always re-queried.
    // Expiry keeps the blocking call bounded (at most one per interval) while
    // preserving recovery. Note this branch is reached for a non-empty path whose
    // file has since disappeared as well, so a dead path re-queries too rather
    // than being served forever.
    constexpr qint64 kNegativeCacheMs = 2000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (s_wallpaperPathResolved && now - s_wallpaperPathResolvedAtMs < kNegativeCacheMs) {
        return s_cachedWallpaperPath;
    }

    if (!s_wallpaperProvider) {
        s_wallpaperProvider = createWallpaperProvider();
    }

    s_cachedWallpaperPath = s_wallpaperProvider->wallpaperPath();
    s_wallpaperPathResolved = true;
    s_wallpaperPathResolvedAtMs = now;
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
        path = s_cachedWallpaperPath; // re-read after relock — local copy may be stale
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
    // Load outside of lock scope is not possible since we write to static cache
    QImage img(path);
    if (img.isNull()) {
        return {};
    }
    s_cachedWallpaperImage = img.convertToFormat(QImage::Format_RGBA8888);
    s_cachedWallpaperMtime = mtime;
    qCDebug(lcShaderRegistry) << "Loaded and cached wallpaper image:" << path << s_cachedWallpaperImage.size();
    return s_cachedWallpaperImage;
}

QRect ShaderRegistry::computeWallpaperCropRect(QSize wpSize, const QRect& physGeom, const QRect& subGeom)
{
    if (wpSize.isEmpty() || !subGeom.isValid() || !physGeom.isValid() || subGeom == physGeom) {
        return {};
    }
    // Only crop when the sub-region actually lies inside the physical screen.
    const QRect clamped = subGeom.intersected(physGeom);
    if (!clamped.isValid() || clamped == physGeom) {
        return {};
    }

    // "Cover" placement of the wallpaper on the physical screen: aspect-correct
    // fill centered, overflow cropped. Mirrors shaders/wallpaper.glsl::wallpaperUv
    // so the cropped image sampled with aspect == subGeom reproduces the same
    // portion of the wallpaper that would appear inside subGeom if the whole
    // physical screen were drawn with the full wallpaper.
    const qreal wpW = wpSize.width();
    const qreal wpH = wpSize.height();
    const qreal physW = physGeom.width();
    const qreal physH = physGeom.height();
    const qreal wpAspect = wpW / qMax<qreal>(wpH, 1.0);
    const qreal physAspect = physW / qMax<qreal>(physH, 1.0);

    qreal coverX, coverY, coverW, coverH;
    if (wpAspect > physAspect) {
        coverH = wpH;
        coverW = wpH * physAspect;
        coverX = (wpW - coverW) * 0.5;
        coverY = 0.0;
    } else {
        coverW = wpW;
        // NOT clamped to 1.0. `physGeom.isValid()` already guarantees height >= 1
        // so `physAspect` is strictly positive, and clamping at 1.0 silently
        // rewrote the crop for every PORTRAIT or rotated output (physAspect < 1),
        // where it must divide by the real aspect. The sibling branch above uses
        // `physAspect` unclamped, and so does the shader this mirrors
        // (data/overlays/shared/wallpaper.glsl: uv.y scaled by wpAspect/scrAspect).
        // Getting this wrong made adjacent virtual screens on such an output
        // sample different portions of the wallpaper, breaking the seam-free
        // tiling the edge-independent maths below exists to guarantee.
        coverH = wpW / physAspect;
        coverX = 0.0;
        coverY = (wpH - coverH) * 0.5;
    }

    // Compute edges (left/top/right/bottom) independently so adjacent VSes
    // tile the wallpaper seam-free: VS-A's right edge equals VS-B's left edge
    // because both are derived from the same coverX + frac*coverW expression
    // on the shared boundary. Deriving width from (right - left) then keeps
    // them tight regardless of how qRound breaks the tie.
    const qreal fracL = (clamped.x() - physGeom.x()) / physW;
    const qreal fracT = (clamped.y() - physGeom.y()) / physH;
    const qreal fracR = (clamped.x() + clamped.width() - physGeom.x()) / physW;
    const qreal fracB = (clamped.y() + clamped.height() - physGeom.y()) / physH;

    const int left = qRound(coverX + fracL * coverW);
    const int top = qRound(coverY + fracT * coverH);
    const int right = qRound(coverX + fracR * coverW);
    const int bottom = qRound(coverY + fracB * coverH);

    const QRect cropRect(left, top, qMax(1, right - left), qMax(1, bottom - top));
    const QRect safe = cropRect.intersected(QRect(QPoint(0, 0), wpSize));
    if (!safe.isValid() || safe.width() < 1 || safe.height() < 1) {
        return {};
    }
    return safe;
}

QImage ShaderRegistry::loadWallpaperImage(const QRect& subGeom, const QRect& physGeom)
{
    QImage full = loadWallpaperImage();
    if (full.isNull() || !subGeom.isValid() || !physGeom.isValid() || subGeom == physGeom) {
        return full;
    }

    QMutexLocker lock(&s_wallpaperCacheMutex);
    // Crops are only valid while the full wallpaper behind them is unchanged.
    // Snapshot the mtime and verify cacheKey() matches the currently-cached
    // full image — if another thread reloaded the wallpaper between our
    // loadWallpaperImage() return and this lock, our `full` is stale relative
    // to s_cachedWallpaperMtime and we must neither read from nor write to
    // the crop cache under that mtime.
    const qint64 mtime = s_cachedWallpaperMtime;
    const bool cacheConsistent = (full.cacheKey() == s_cachedWallpaperImage.cacheKey());

    if (cacheConsistent) {
        // Cache hit: return the stored QImage (stable cacheKey() so downstream
        // setWallpaperTexture equality checks short-circuit correctly).
        for (const auto& entry : s_cachedWallpaperCrops) {
            if (entry.mtime == mtime && !entry.img.isNull() && entry.sub == subGeom && entry.phys == physGeom) {
                return entry.img;
            }
        }
    }

    const QRect safe = computeWallpaperCropRect(full.size(), physGeom, subGeom);
    if (!safe.isValid()) {
        return full;
    }

    QImage cropped = full.copy(safe);
    if (cropped.isNull()) {
        return full;
    }

    if (cacheConsistent) {
        // Insert into the ring-buffer cache. Small fixed capacity — typical
        // systems have at most a handful of VSes so an LRU isn't worth the
        // bookkeeping; oldest entry is overwritten.
        s_cachedWallpaperCrops[s_cachedWallpaperCropNextSlot] = {subGeom, physGeom, mtime, cropped};
        s_cachedWallpaperCropNextSlot = (s_cachedWallpaperCropNextSlot + 1) % CropCacheCapacity;
    }
    return cropped;
}

void ShaderRegistry::invalidateWallpaperCache()
{
    QMutexLocker lock(&s_wallpaperCacheMutex);
    s_cachedWallpaperPath.clear();
    s_wallpaperPathResolved = false;
    s_wallpaperPathResolvedAtMs = 0;
    s_cachedWallpaperImage = QImage();
    s_cachedWallpaperMtime = 0;
    for (auto& entry : s_cachedWallpaperCrops) {
        entry = {};
    }
    s_cachedWallpaperCropNextSlot = 0;
    s_wallpaperProvider.reset(); // force re-detection on next call
}

} // namespace PhosphorShaders

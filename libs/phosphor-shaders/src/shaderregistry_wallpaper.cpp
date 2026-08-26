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
    // BOTH answers are cached for a bounded window only — never latched for
    // the process lifetime.
    //
    // Why cache at all: without this every caller re-queries the provider —
    // and the Hyprland/sway provider spawns a QProcess and blocks in
    // waitForFinished(1000), once per overlay show and once per shader attach.
    //
    // Why it must EXPIRE rather than latch: `invalidateWallpaperCache()` has
    // no callers, so there is no event that would clear it. A permanent
    // NEGATIVE latch (resolve before plasmashell has written its config, or
    // before swww is up) leaves every wallpaper-using shader on the
    // transparent fallback for the whole session; a permanent POSITIVE latch
    // means switching wallpaper to a different file is never picked up while
    // the old file still exists on disk. Expiry keeps the blocking call
    // bounded (at most one per interval) while preserving recovery in both
    // directions. A non-empty path whose file has disappeared re-queries
    // immediately rather than waiting out its window.
    constexpr qint64 kPositiveCacheMs = 5000;
    constexpr qint64 kNegativeCacheMs = 2000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker lock(&s_wallpaperCacheMutex);
        if (s_wallpaperPathResolved) {
            // A live path gets the longer window; an empty or dead one the
            // shorter — but a dead path is still served inside its window so
            // a deleted wallpaper cannot drive one provider query per call.
            const qint64 age = now - s_wallpaperPathResolvedAtMs;
            const bool alive = !s_cachedWallpaperPath.isEmpty() && QFile::exists(s_cachedWallpaperPath);
            if (age < (alive ? kPositiveCacheMs : kNegativeCacheMs)) {
                return s_cachedWallpaperPath;
            }
        }
    }

    // Query the provider OUTSIDE the lock — it may block up to a second in a
    // QProcess wait, and holding the mutex through that stalls every
    // concurrent wallpaper consumer. The provider handle itself is only
    // created here and reset in invalidateWallpaperCache() (which has no
    // callers); a hypothetical concurrent reset is documented there.
    std::unique_ptr<IWallpaperProvider> freshProvider;
    IWallpaperProvider* provider = nullptr;
    {
        QMutexLocker lock(&s_wallpaperCacheMutex);
        provider = s_wallpaperProvider.get();
    }
    if (!provider) {
        freshProvider = createWallpaperProvider();
        provider = freshProvider.get();
    }
    const QString resolved = provider->wallpaperPath();

    QMutexLocker lock(&s_wallpaperCacheMutex);
    if (freshProvider && !s_wallpaperProvider) {
        s_wallpaperProvider = std::move(freshProvider);
    }
    s_cachedWallpaperPath = resolved;
    s_wallpaperPathResolved = true;
    s_wallpaperPathResolvedAtMs = QDateTime::currentMSecsSinceEpoch();
    return s_cachedWallpaperPath;
}

QImage ShaderRegistry::loadWallpaperImage()
{
    const QString path = wallpaperPath(); // resolves (bounded-TTL cached) under its own locking
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

    // Cache check under the lock; the decode itself runs WITHOUT the lock — a
    // 4K wallpaper decode+convert is tens of milliseconds, and holding the
    // process-wide mutex through it stalls every concurrent wallpaper
    // consumer. (QImage is implicitly shared, so the returned copy is cheap.)
    {
        QMutexLocker lock(&s_wallpaperCacheMutex);
        if (!s_cachedWallpaperImage.isNull() && s_cachedWallpaperMtime == mtime) {
            return s_cachedWallpaperImage;
        }
    }

    QImage img(path);
    if (img.isNull()) {
        return {};
    }
    QImage converted = img.convertToFormat(QImage::Format_RGBA8888);

    QMutexLocker lock(&s_wallpaperCacheMutex);
    // Recheck after re-acquiring: another thread may have decoded and stored
    // the same (or a fresher) image while we were outside the lock. Prefer
    // the stored one so downstream cacheKey() equality checks short-circuit.
    if (!s_cachedWallpaperImage.isNull() && s_cachedWallpaperMtime == mtime) {
        return s_cachedWallpaperImage;
    }
    s_cachedWallpaperImage = std::move(converted);
    s_cachedWallpaperMtime = mtime;
    qCDebug(lcShaderRegistry) << "Loaded and cached wallpaper image:" << path << s_cachedWallpaperImage.size();
    return s_cachedWallpaperImage;
}

namespace {

/// The wallpaper's "cover" placement over a screen, in wallpaper pixels:
/// aspect-correct fill, centred, overflow cropped.
///
/// Factored out because two callers need the identical placement and only
/// differ in what they do with it — one cuts an image out of it, the other maps
/// it across a quad. Two copies of this would be two chances to disagree with
/// shaders/wallpaper.glsl::wallpaperUv, which both of them mirror.
struct WallpaperCover
{
    qreal x = 0.0;
    qreal y = 0.0;
    qreal w = 0.0;
    qreal h = 0.0;
};

WallpaperCover wallpaperCoverFor(QSize wpSize, const QRect& physGeom)
{
    const qreal wpW = wpSize.width();
    const qreal wpH = wpSize.height();
    const qreal physW = physGeom.width();
    const qreal physH = physGeom.height();
    const qreal wpAspect = wpW / qMax<qreal>(wpH, 1.0);
    // NOT clamped to 1.0. physGeom.isValid() guarantees height >= 1 so this is
    // strictly positive, and clamping silently rewrote the crop for every
    // PORTRAIT or rotated output (physAspect < 1), where it must divide by the
    // real aspect. The shader this mirrors uses it unclamped too
    // (data/overlays/shared/wallpaper.glsl: uv.y scaled by wpAspect/scrAspect).
    // Getting it wrong made adjacent virtual screens on such an output sample
    // different portions of the wallpaper, which breaks the seam-free tiling
    // computeWallpaperCropRect's edge-independent maths exists to guarantee.
    const qreal physAspect = physW / qMax<qreal>(physH, 1.0);

    WallpaperCover cover;
    if (wpAspect > physAspect) {
        cover.h = wpH;
        cover.w = wpH * physAspect;
        cover.x = (wpW - cover.w) * 0.5;
        cover.y = 0.0;
    } else {
        cover.w = wpW;
        cover.h = wpW / physAspect;
        cover.x = 0.0;
        cover.y = (wpH - cover.h) * 0.5;
    }
    return cover;
}

} // namespace

std::optional<QRectF> ShaderRegistry::wallpaperSliceNormalized(QSize wpSize, const QRect& physGeom,
                                                               const QRect& subGeom)
{
    if (wpSize.isEmpty() || !subGeom.isValid() || !physGeom.isValid()) {
        return std::nullopt;
    }
    const WallpaperCover cover = wallpaperCoverFor(wpSize, physGeom);

    // Fractions of the SCREEN, taken from the raw subGeom so an overhanging
    // surface produces an out-of-range slice rather than a clamped one.
    const qreal physW = physGeom.width();
    const qreal physH = physGeom.height();
    const qreal fracL = (subGeom.x() - physGeom.x()) / physW;
    const qreal fracT = (subGeom.y() - physGeom.y()) / physH;
    const qreal fracW = subGeom.width() / physW;
    const qreal fracH = subGeom.height() / physH;

    const qreal wpW = wpSize.width();
    const qreal wpH = wpSize.height();
    return QRectF((cover.x + fracL * cover.w) / wpW, (cover.y + fracT * cover.h) / wpH, (fracW * cover.w) / wpW,
                  (fracH * cover.h) / wpH);
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
    //
    // The placement itself is wallpaperCoverFor's, shared with
    // wallpaperSliceNormalized. Only what the two do with it differs: this one
    // cuts an image out, the other maps it across a quad. A second copy here
    // would be a second chance to disagree with the shader both of them mirror,
    // and it would carry its own duplicate of the unclamped-physAspect note
    // that copy needed.
    const qreal physW = physGeom.width();
    const qreal physH = physGeom.height();
    const WallpaperCover cover = wallpaperCoverFor(wpSize, physGeom);
    const qreal coverX = cover.x;
    const qreal coverY = cover.y;
    const qreal coverW = cover.w;
    const qreal coverH = cover.h;

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
    // Currently has no callers. If one appears, note that wallpaperPath()
    // queries the provider outside the lock, so resetting the provider here
    // concurrently with an in-flight query would destroy it mid-use — a
    // caller must guarantee no concurrent resolution (or this must move to a
    // shared_ptr handoff).
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

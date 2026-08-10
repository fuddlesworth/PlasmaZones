// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace PlasmaZones {

/**
 * @brief QML image provider + bounded LRU cache for Snap Assist window thumbnails.
 *
 * Replaces the unbounded `QHash<QString, QString>` of base64-encoded PNG data
 * URLs that the previous design accumulated for the daemon's whole session.
 * Two wins:
 *
 *   - Hard memory cap. The cache is BYTE-denominated: @ref CacheMaxBytes
 *     (@ref CacheCapacity × 256² ARGB32 ≈ 6 MB) bounds resident pixel data
 *     regardless of per-image size — the boundary admits up to 1024² (4 MiB)
 *     per image, so a count-based cap of the same 24 entries would really
 *     have been a ~96 MB ceiling. Long sessions that touch many windows
 *     discard the oldest entries instead of growing forever. Per-handle URL
 *     state lives inside the cache entry, so eviction reclaims it together
 *     with the image — no out-of-band map that grows independently.
 *   - No PNG-encode + base64 round-trip. Captures land as @c QImage and stay
 *     as @c QImage; QML loads them through this provider on its image-loader
 *     thread.
 *
 * QML references thumbnails via the URL scheme
 * `image://plasmazones-snapassist/<compositorHandle>/<generation>`. The
 * generation token is bumped from a single monotonic counter on every
 * @ref insert so QML's own QQuickPixmap cache re-fetches when a fresh capture
 * arrives. Handles arrive from KWin in braced form
 * (`{xxxxxxxx-xxxx-...}`); we normalise to the unbraced UUID form before
 * composing the URL so the path component contains only RFC-3986
 * unreserved characters and survives any QUrl percent-encoding policy
 * change unchanged. Cache keys are stored in the same unbraced form so
 * @ref insert, @ref urlFor and @ref requestImage accept either spelling
 * from callers. A handle that is empty after normalisation or contains a
 * '/' is REJECTED at insert (empty URL → producer sees accepted=false):
 * requestImage splits its id at the first '/', so such a handle could be
 * stored but never looked up — see ThumbnailUrl::isValidHandleKey.
 *
 * @ref requestImage runs on Qt's image-loader worker thread; all reads and
 * writes go through @ref m_mutex.
 */
class SnapAssistThumbnailProvider : public QQuickImageProvider
{
public:
    /// QML provider id — the host segment of `image://<id>/...`.
    static constexpr const char* ProviderId = "plasmazones-snapassist";
    /// Steady-state entry budget. Sourced from the shared PhosphorProtocol
    /// constant so the daemon and the kwin-effect's recent-posted dedup set
    /// are always sized off a single literal — capacity drift across
    /// rebuilds is impossible.
    ///
    /// Note: only the *sizing input* is shared. Eviction differs — this
    /// side is byte-cost LRU over @ref CacheMaxBytes (urlFor / requestImage
    /// promotes recency), the effect is a plain FIFO of first-post times
    /// capped at this entry COUNT. The two agree exactly at the effect's
    /// 256² capture size; any capture ABOVE it makes this side evict
    /// earlier than the effect's window expects, and because the effect's
    /// skip path re-promotes, the affected candidate then shows its icon
    /// until the trim signal (snapAssistThumbnailCacheTrimmed) resets the
    /// effect's dedup set — not merely until the next show. Bounded, but
    /// worth knowing when tuning capture sizes; see the matching comment on
    /// @c SnapAssistThumbnailCapture::RecentPostedCapacity for the full
    /// treatment.
    static constexpr int CacheCapacity = PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity;
    /// Byte-denominated cache bound: the steady-state entry budget at the
    /// effect's 256² ARGB32 capture size. Inserts carry their image's real
    /// byte size as cost, so oversized images (the boundary ceiling is
    /// 1024² = 4 MiB) consume proportionally more of the budget instead of
    /// silently multiplying the documented cap.
    static constexpr int CacheMaxBytes = CacheCapacity * 256 * 256 * 4;

    SnapAssistThumbnailProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /**
     * @brief Insert (or update) the thumbnail for @p compositorHandle.
     * @return the QML URL the caller hands to the @c Image element. Embeds a
     *         monotonically-increasing generation counter so the URL changes
     *         on every insert and QML re-fetches the (now-updated) image.
     */
    QString insert(const QString& compositorHandle, QImage image);

    /**
     * @brief Look up the current QML URL for @p compositorHandle without
     *        inserting. Returns an empty string when the thumbnail isn't in
     *        the cache (either never captured or evicted under LRU pressure).
     *        Used to apply cached thumbnails to a fresh candidate list during
     *        a snap-assist continuation.
     */
    QString urlFor(const QString& compositorHandle) const;

    /**
     * @brief Drop every cached thumbnail, releasing their pixel buffers.
     *
     * The cache is already hard-capped and LRU-evicting, but after the final
     * snap-assist of a session its (≈6 MB worst-case) images would otherwise
     * sit resident until daemon exit. OverlayService calls this on a short
     * idle-grace timer after snap-assist stays dismissed, so rapid
     * continuations keep the warm cache while a genuine end-of-use reclaims
     * it; the next snap-assist repopulates from fresh kwin-effect captures.
     */
    void clear();

private:
    /// Cache entry — image plus the URL that names this generation. Storing
    /// the URL inside the entry means LRU eviction reclaims the URL state
    /// together with the image; no separate per-handle map can grow without
    /// bound across long sessions.
    struct Entry
    {
        QImage image;
        QString url;
    };

    mutable QMutex m_mutex;
    QCache<QString, Entry> m_cache;
    /// Monotonic across the daemon's lifetime; never wraps in any realistic
    /// session (32 bits = 4 G captures). Each insert produces a strictly
    /// new URL, so QML's QQuickPixmap cache always re-fetches.
    quint32 m_generation = 0;
};

} // namespace PlasmaZones

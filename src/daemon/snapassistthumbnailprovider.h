// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
 *   - Hard memory cap. @ref kCacheCapacity entries × 256² ARGB32 ≈ 6 MB worst
 *     case. Long sessions that touch many windows discard the oldest entries
 *     instead of growing forever. Per-handle URL state lives inside the
 *     cache entry, so eviction reclaims it together with the image — no
 *     out-of-band map that grows independently.
 *   - No PNG-encode + base64 round-trip. Captures land as @c QImage and stay
 *     as @c QImage; QML loads them through this provider on its image-loader
 *     thread.
 *
 * QML references thumbnails via the URL scheme
 * `image://plasmazones-snapassist/<compositorHandle>/<generation>`. The
 * generation token is bumped from a single monotonic counter on every
 * @ref insert so QML's own QQuickPixmap cache re-fetches when a fresh capture
 * arrives. The compositor-handle segment is a braced UUID
 * (KWin EffectWindow::internalId.toString()) — `{xxxxxxxx-xxxx-...}` — which
 * contains no `/`, so a single split on the first separator recovers it
 * cleanly.
 *
 * @ref requestImage runs on Qt's image-loader worker thread; all reads and
 * writes go through @ref m_mutex.
 */
class SnapAssistThumbnailProvider : public QQuickImageProvider
{
public:
    /// QML provider id — the host segment of `image://<id>/...`.
    static constexpr const char* kProviderId = "plasmazones-snapassist";
    /// Hard upper bound on cached thumbnails. 24 × 256² ARGB32 ≈ 6 MB.
    static constexpr int kCacheCapacity = 24;

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

    static QString makeUrl(const QString& handle, quint32 generation);

    mutable QMutex m_mutex;
    QCache<QString, Entry> m_cache;
    /// Monotonic across the daemon's lifetime; never wraps in any realistic
    /// session (32 bits = 4 G captures). Each insert produces a strictly
    /// new URL, so QML's QQuickPixmap cache always re-fetches.
    quint32 m_generation = 0;
};

} // namespace PlasmaZones

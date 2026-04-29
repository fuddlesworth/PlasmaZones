// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QVector>

#include <memory>

class QImage;

namespace KWin {
class OffscreenQuickScene;
}

namespace PlasmaZones {

/**
 * @brief Captures snap-assist window thumbnails inside the KWin process.
 *
 * Uses @c KWin::OffscreenQuickScene plus the @c WindowThumbnail QML element
 * from @c org.kde.kwin to render each candidate window through the live
 * compositor texture and grab the result as a QImage. No ScreenShot2 D-Bus
 * round-trip, no daemon-side @c X-KDE-DBUS-Restricted-Interfaces gate, no
 * second render of the window — KWin reuses the texture it already has.
 *
 * Captures run sequentially through one shared scene so at most one
 * QSGRenderContext + grab pass is in flight, mirroring the throttling the
 * daemon's previous ScreenShot2 path needed (concurrent CaptureWindow calls
 * could starve KWin's screenshot queue). Each completed image is posted
 * back to the daemon via @c org.plasmazones.Overlay.setSnapAssistThumbnail
 * as raw ARGB32 (non-premultiplied) bytes plus dimensions — no PNG encode,
 * no base64. The daemon validates the buffer shape and copies the bytes
 * into a QImage that lands in its bounded LRU cache.
 */
class SnapAssistThumbnailCapture : public QObject
{
    Q_OBJECT

public:
    /// Each candidate is just a QUuid — the EffectWindow internal id that
    /// WindowThumbnail's @c wId property accepts. The braced @c toString()
    /// form is also the daemon's image-provider cache key, so we derive it
    /// once at post time inside @ref postThumbnail rather than carrying both
    /// representations through every queue entry.
    struct Candidate
    {
        QUuid internalId;
    };

    explicit SnapAssistThumbnailCapture(QObject* parent = nullptr);
    ~SnapAssistThumbnailCapture() override;

    /// Default thumbnail bounding box. C++ overrides @c boxSize on the
    /// QML root before every render via setProperty, so the QML literal
    /// fallback (in SnapAssistThumb.qml) is unreachable in production —
    /// it exists only to keep the QML scene previewable in Qt Creator's
    /// QML tooling. Kept here as the canonical value and as the default
    /// argument for @ref captureCandidates; if you change the size,
    /// update the QML literal so design-time previews still match.
    static constexpr QSize DefaultThumbnailSize = QSize(256, 256);

    /**
     * @brief Queue captures for the given candidates.
     *
     * Replaces any pending queue from a prior snap-assist invocation: when
     * the user finishes one snap and immediately starts another, the older
     * candidate set is stale and shouldn't burn render budget. The queue is
     * drained one capture at a time — see the @c processNext loop.
     *
     * @param maxSize Bounding box for each thumbnail; aspect ratio
     *        preserved by WindowThumbnail itself.
     */
    void captureCandidates(const QVector<Candidate>& candidates, QSize maxSize = DefaultThumbnailSize);

    /**
     * @brief Drop the recently-posted bookkeeping.
     *
     * Called when the daemon's cache is known to be empty (daemon restart,
     * service registration after a disconnect). Without this the kwin-effect
     * would keep skipping captures for handles the daemon no longer holds,
     * stranding snap-assist on icons until the LRU window rolls past.
     */
    void resetRecentlyPosted();

private Q_SLOTS:
    void processNext();

private:
    void ensureScene();
    void postThumbnail(const QUuid& internalId, const QImage& image);

    /// Common cleanup for early-bail paths in @ref processNext and the
    /// @ref attemptCapture timer lambda: drop the in-flight queue and
    /// release @c m_busy so a subsequent @ref captureCandidates can
    /// dispatch fresh work. Leaving @c m_busy=true without a follow-up
    /// dispatch would silently wedge the queue forever, so every error
    /// exit must route through here (or set both fields inline).
    void dropQueueAndIdle();

    struct Pending
    {
        QUuid internalId;
        QSize maxSize;
    };

    /// Read back the current scene buffer for @p p; on a null buffer, retry
    /// once with a longer delay before giving up. Compositor stalls
    /// occasionally drop the very first frame after binding @c wId to a
    /// fresh window; one retry is enough in practice and falls back to the
    /// icon path otherwise.
    void attemptCapture(Pending p, int delayMs, int retriesLeft);

    /// Mark @p handle as posted to the daemon, evicting the oldest entry
    /// if the bookkeeping set is at capacity. Called from the D-Bus
    /// pending-call success path inside @ref postThumbnail so failed sends
    /// leave the handle un-tracked and the next snap-assist invocation
    /// retries.
    void markRecentlyPosted(const QUuid& handle);

    /// True when @p handle was recently posted to the daemon and is
    /// (probably) still resident in the daemon's bounded LRU cache.
    /// Mirrors the daemon's @c SnapAssistThumbnailProvider::CacheCapacity
    /// so this side stays in sync with the daemon's eviction window.
    bool wasRecentlyPosted(const QUuid& handle) const;

    /// Mirror of the daemon-side LRU cap, sourced from the shared
    /// @c PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity
    /// constant rather than a duplicate literal. A single bump in the
    /// shared header re-aligns both sides on rebuild — capacity drift
    /// across rebuilds is impossible.
    ///
    /// Eviction *order* still differs across the boundary: the daemon
    /// is true-LRU (its QCache promotes recency on every @c urlFor /
    /// @c requestImage hit), this side is plain FIFO of first-post
    /// times. Both drift directions have bounded fallbacks — if the
    /// daemon evicts ahead of this FIFO, the effect skips a re-capture
    /// and snap-assist falls back to icons until the FIFO rolls past;
    /// if this FIFO evicts ahead of the daemon, the next capture is
    /// wasted (re-posted into a slot the daemon already held). For the
    /// daemon-restart case where the gap is unbounded,
    /// @ref resetRecentlyPosted clears the set on daemon-ready
    /// transitions so a cold daemon cache also resets the dedup state.
    static constexpr int RecentPostedCapacity = PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity;
    static_assert(RecentPostedCapacity > 0,
                  "RecentPostedCapacity must be positive — the eviction loop in markRecentlyPosted "
                  "assumes the just-inserted handle survives the capacity check.");

    std::unique_ptr<KWin::OffscreenQuickScene> m_scene;
    QQueue<Pending> m_queue;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership via the set,
    /// O(1) oldest-first eviction via the queue. Kept strictly in sync.
    QSet<QUuid> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

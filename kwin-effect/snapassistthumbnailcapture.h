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
class EffectWindow;
}

namespace PlasmaZones {

/**
 * @brief Captures snap-assist window thumbnails inside the KWin process.
 *
 * Renders each candidate @c KWin::EffectWindow into an offscreen
 * @c GLFramebuffer (via @c effects->drawWindow) and reads it back with
 * @c GLTexture::toImage(). This reuses the live compositor texture KWin
 * already holds for the window — no ScreenShot2 D-Bus round-trip, no
 * daemon-side @c X-KDE-DBUS-Restricted-Interfaces gate, no second scene-graph
 * pass over the window.
 *
 * KWin 6.7 removed the public offscreen-QML readback (@c OffscreenQuickView
 * lost @c bufferAsImage() and @c update() now requires an @c OutputFrame), so
 * the earlier @c OffscreenQuickScene + @c WindowThumbnail QML approach is no
 * longer available; the direct GLFramebuffer render is its replacement.
 *
 * Captures run sequentially, one render+readback at a time, mirroring the
 * throttling the daemon's previous ScreenShot2 path needed (concurrent
 * CaptureWindow calls could starve KWin's screenshot queue). Each completed
 * image is posted
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
    /// Render @p w into an offscreen GLFramebuffer fit within @p box (aspect
    /// ratio preserved) and read it back as a straight-alpha ARGB32 QImage.
    /// Returns a null image if the window can't be found/rendered. MUST run on
    /// the compositor thread; it makes the GL context current itself, so it is
    /// safe to call outside a paint pass (KWin 6.7's drawWindow/GLFramebuffer
    /// path needs no OutputFrame).
    QImage grabWindowImage(KWin::EffectWindow* w, QSize box) const;
    void postThumbnail(const QUuid& internalId, const QImage& image);

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

    /// Mark @p handle as posted to the daemon, evicting the least-recently-
    /// used entry if the bookkeeping is at capacity. Called from the D-Bus
    /// pending-call success path inside @ref postThumbnail so failed sends
    /// leave the handle un-tracked and the next snap-assist invocation
    /// retries. Re-marking an already-tracked handle bumps it to the
    /// most-recently-used end (mirrors the daemon's QCache promote-on-insert).
    void markRecentlyPosted(const QUuid& handle);

    /// True when @p handle was recently posted to the daemon and is
    /// (probably) still resident in the daemon's bounded LRU cache.
    /// Mirrors the daemon's @c SnapAssistThumbnailProvider::CacheCapacity
    /// so this side stays in sync with the daemon's eviction window.
    bool wasRecentlyPosted(const QUuid& handle) const;

    /// Move @p handle to the most-recently-used end of the order queue.
    /// Called from the @ref captureCandidates skip-recapture path AND the
    /// @ref markRecentlyPosted re-mark path so this side mirrors the
    /// daemon's QCache promote-on-access semantics. Without this, a
    /// frequently re-snapped window drifts out of the bookkeeping window
    /// in the order it was first posted, even though the daemon would
    /// keep it MRU and never evict it — re-capturing redundantly when
    /// the daemon still holds the entry.
    /// No-op if @p handle isn't in the order queue (defensive against
    /// invariant violations).
    void bumpRecency(const QUuid& handle);

    /// Mirror of the daemon-side LRU cap, sourced from the shared
    /// @c PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity
    /// constant rather than a duplicate literal. A single bump in the
    /// shared header re-aligns both sides on rebuild — capacity drift
    /// across rebuilds is impossible.
    ///
    /// Both sides are LRU: the daemon's QCache promotes recency on every
    /// @c urlFor / @c requestImage hit; this side promotes via
    /// @ref bumpRecency on every "we used the dedup state" event
    /// (skip-recapture decision in @ref captureCandidates and re-mark
    /// in @ref markRecentlyPosted). Promotion semantics aren't perfectly
    /// symmetric — the daemon also promotes on QML's @c requestImage
    /// (paint-time) which the effect can't observe — but the residual
    /// drift in either direction has bounded fallbacks: if the daemon
    /// evicts ahead of this side, the effect skips re-capture and
    /// snap-assist falls back to icons; if this side evicts ahead of
    /// the daemon, the next capture is a wasted re-post. For the
    /// daemon-restart case where the gap is unbounded,
    /// @ref resetRecentlyPosted clears the set on daemon-ready
    /// transitions so a cold daemon cache also resets the dedup state.
    static constexpr int RecentPostedCapacity = PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity;
    static_assert(RecentPostedCapacity > 0,
                  "RecentPostedCapacity must be positive — the eviction loop in markRecentlyPosted "
                  "assumes the just-inserted handle survives the capacity check.");

    QQueue<Pending> m_queue;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership via the set,
    /// O(1) oldest-first eviction via the queue. Kept strictly in sync.
    QSet<QUuid> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

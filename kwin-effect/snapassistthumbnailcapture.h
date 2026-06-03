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

#include <array>
#include <cstdint>
#include <memory>

class QImage;

namespace KWin {
class OffscreenQuickScene;
class GLTexture;
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
    /// Single-plane DMA-BUF exported from a rendered thumbnail texture (the
    /// ExportMode::Texture path). @c ok is false when the compositor is not on
    /// an EGL/GL backend or the driver lacks EGL_MESA_image_dma_buf_export.
    /// Per-export failures and daemon import rejections are counted; after
    /// @ref DmabufFailureThreshold consecutive failures the capture switches
    /// the whole session back to the raw-pixel path (@ref onDmabufRejected) so
    /// an unsupported setup degrades to working pixel thumbnails instead of
    /// stranding candidates on icons.
    struct DmabufExport
    {
        bool ok = false;
        int fd = -1;
        int fenceFd = -1; ///< sync_file fence: signals when the GL render completed.
        int width = 0;
        int height = 0;
        uint32_t fourcc = 0;
        uint64_t modifier = 0;
        uint32_t stride = 0;
        uint32_t offset = 0;
    };

    struct Pending
    {
        QUuid internalId;
        QSize maxSize;
    };

    /// Pick (and lazily create) the producer scene for the next capture and
    /// set it as @ref m_activeScene's source. In the dma-buf path this
    /// round-robins across the pool; in the raw-pixel path it always returns
    /// slot 0. Returns nullptr only if scene creation failed.
    KWin::OffscreenQuickScene* acquireSceneForCapture();
    void postThumbnail(const QUuid& internalId, const QImage& image);
    void postThumbnailDmabuf(const Pending& p, const DmabufExport& exported);

    /// Record a dma-buf capture failure (export failure or daemon import
    /// rejection) for @p p. After @ref DmabufFailureThreshold consecutive
    /// failures, permanently switches this session to the raw-pixel path:
    /// clears @c m_dmabufEnabled, resets the scene (so it rebuilds in
    /// ExportMode::Image), and re-enqueues @p p so it re-captures via pixels.
    /// Pure state mutation — the caller owns kicking @ref processNext.
    void onDmabufRejected(const Pending& p);

    /// Export a rendered thumbnail texture (from OffscreenQuickScene in
    /// ExportMode::Texture) to a single-plane dma-buf via
    /// EGL_MESA_image_dma_buf_export. Must be called with KWin's GL/EGL
    /// context current (i.e. right after OffscreenQuickScene::update()).
    /// Returns {ok=false} on any failure; the caller drops the candidate.
    DmabufExport exportTextureToDmabuf(KWin::GLTexture* texture) const;

    /// Common cleanup for early-bail paths in @ref processNext and the
    /// @ref attemptCapture timer lambda: drop the in-flight queue and
    /// release @c m_busy so a subsequent @ref captureCandidates can
    /// dispatch fresh work. Leaving @c m_busy=true without a follow-up
    /// dispatch would silently wedge the queue forever, so every error
    /// exit must route through here (or set both fields inline).
    void dropQueueAndIdle();

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

    /// Opt-in zero-copy GPU path (PLASMAZONES_DMABUF_THUMBNAILS). When set,
    /// the scene is built in ExportMode::Texture and each capture is exported
    /// as a dma-buf and posted via setWindowThumbnailDmabuf instead of the
    /// raw-ARGB32 setSnapAssistThumbnail. Initialised from the env var at
    /// construction; cleared by @ref onDmabufRejected if the path proves
    /// unavailable at runtime, after which the session uses the pixel path.
    bool m_dmabufEnabled = false;
    /// Consecutive dma-buf capture failures (export or daemon rejection).
    /// Reset on success; triggers the session fallback at
    /// @ref DmabufFailureThreshold.
    int m_dmabufConsecutiveFailures = 0;

    /// Small producer pool of capture scenes. In the dma-buf path consecutive
    /// captures round-robin across the pool so a producer buffer isn't reused
    /// until ScenePoolSize captures later — long enough for the daemon to have
    /// copied it into its own per-candidate texture (see DmabufQsgTexture). The
    /// raw-pixel path uses slot 0 only (bufferAsImage copies immediately, so no
    /// producer-buffer aliasing). @ref m_activeScene is the slot driving the
    /// current in-flight capture (a borrowed pointer into the pool).
    static constexpr int ScenePoolSize = 3;
    std::array<std::unique_ptr<KWin::OffscreenQuickScene>, ScenePoolSize> m_scenePool;
    int m_poolNext = 0;
    /// Export mode the live pool slots were built for. When it diverges from
    /// m_dmabufEnabled (a runtime fallback to pixels), acquireSceneForCapture
    /// rebuilds the pool at the next idle capture — never mid-flight.
    bool m_poolBuiltForTextureMode = false;
    KWin::OffscreenQuickScene* m_activeScene = nullptr;
    QQueue<Pending> m_queue;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership via the set,
    /// O(1) oldest-first eviction via the queue. Kept strictly in sync.
    QSet<QUuid> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

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
class EffectWindow;
class GLTexture;
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
 * image is posted back to the daemon via
 * @c org.plasmazones.Overlay.setSnapAssistThumbnail as raw ARGB32
 * (non-premultiplied) bytes plus dimensions — no PNG encode, no base64. The
 * daemon validates the buffer shape and copies the bytes into a QImage that
 * lands in its bounded LRU cache.
 *
 * An opt-in zero-copy GPU path (@c PLASMAZONES_DMABUF_THUMBNAILS) exports the
 * rendered FBO texture as a single-plane dma-buf and ships the fd via
 * @c setWindowThumbnailDmabuf instead of the raw bytes. It degrades to the
 * raw-pixel path automatically when the driver lacks the required EGL
 * extensions or the daemon repeatedly rejects the import (@ref onDmabufRejected).
 */
class SnapAssistThumbnailCapture : public QObject
{
    Q_OBJECT

public:
    /// Each candidate is just a QUuid — the EffectWindow internal id passed to
    /// @c effects->findWindow() to locate the window to render. The braced
    /// @c toString() form is also the daemon's image-provider cache key, so we
    /// derive it once at post time inside @ref postThumbnail rather than
    /// carrying both representations through every queue entry.
    struct Candidate
    {
        QUuid internalId;
    };

    explicit SnapAssistThumbnailCapture(QObject* parent = nullptr);
    ~SnapAssistThumbnailCapture() override;

    /// Default thumbnail bounding box. The captured window is fit within this
    /// box (aspect ratio preserved) by @ref grabWindowImage. Used as the
    /// default argument for @ref captureCandidates.
    static constexpr QSize DefaultThumbnailSize = QSize(256, 256);

    /**
     * @brief Queue captures for the given candidates.
     *
     * Replaces any pending queue from a prior snap-assist invocation: when
     * the user finishes one snap and immediately starts another, the older
     * candidate set is stale and shouldn't burn render budget. The queue is
     * drained one capture at a time — see the @c processNext loop.
     *
     * @param maxSize Bounding box for each thumbnail; aspect ratio is
     *        preserved by @ref grabWindowImage.
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
    /// Single-plane DMA-BUF exported from a rendered thumbnail texture. @c ok
    /// is false when the compositor is not on an EGL/GL backend or the driver
    /// lacks EGL_MESA_image_dma_buf_export. Per-export failures and daemon
    /// import rejections are counted; after @ref DmabufFailureThreshold
    /// consecutive failures the capture switches the whole session back to the
    /// raw-pixel path (@ref onDmabufRejected) so an unsupported setup degrades
    /// to working pixel thumbnails instead of stranding candidates on icons.
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

    /// Render @p w into an offscreen GLFramebuffer fit within @p box (aspect
    /// ratio preserved) and read it back as a straight-alpha ARGB32 QImage.
    /// Returns a null image if the window can't be found/rendered. MUST run on
    /// the compositor thread; it makes the GL context current itself, so it is
    /// safe to call outside a paint pass (KWin 6.7's drawWindow/GLFramebuffer
    /// path needs no OutputFrame). This is the raw-pixel path.
    QImage grabWindowImage(KWin::EffectWindow* w, QSize box) const;

    /// Render @p w into a pooled, persistent GLFramebuffer texture and return
    /// it (a borrowed pointer into @ref m_texturePool). Used by the dma-buf
    /// path: the exported buffer aliases the texture, so it must outlive the
    /// daemon's async import — the pool round-robins so a slot isn't reused
    /// until @ref TexturePoolSize captures later, giving the daemon's copy a
    /// margin (mirroring the producer-scene pool the OffscreenQuickScene path
    /// used). Returns nullptr if the window can't be found/rendered.
    ///
    /// Unlike @ref grabWindowImage this does NOT manage GL context currency:
    /// the caller makes the compositor context current before calling and
    /// keeps it current through the subsequent @ref exportTextureToDmabuf
    /// (which reads it via @c eglGetCurrentContext).
    KWin::GLTexture* renderWindowToPooledTexture(KWin::EffectWindow* w, QSize box);

    void postThumbnail(const QUuid& internalId, const QImage& image);
    void postThumbnailDmabuf(const Pending& p, const DmabufExport& exported);

    /// Record a dma-buf capture failure (export failure or daemon import
    /// rejection) for @p p. After @ref DmabufFailureThreshold consecutive
    /// failures, permanently switches this session to the raw-pixel path
    /// (clears @c m_dmabufEnabled) and re-enqueues @p p so it re-captures via
    /// pixels. Pure state mutation — the caller owns kicking @ref processNext.
    void onDmabufRejected(const Pending& p);

    /// Export a rendered thumbnail texture to a single-plane dma-buf via
    /// EGL_MESA_image_dma_buf_export. Must be called with KWin's GL/EGL
    /// context current (i.e. while @ref renderWindowToPooledTexture's context
    /// is still active). Returns {ok=false} on any failure; the caller drops
    /// the candidate.
    DmabufExport exportTextureToDmabuf(KWin::GLTexture* texture) const;

    /// Render and read back the thumbnail for @p p; on a null image, retry
    /// once with a longer delay before giving up. A freshly mapped window
    /// occasionally has no renderable frame on the first attempt; one retry is
    /// enough in practice and falls back to the icon path otherwise.
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
    /// each capture renders into a pooled FBO texture, exports it as a dma-buf
    /// and posts via setWindowThumbnailDmabuf instead of the raw-ARGB32
    /// setSnapAssistThumbnail. Initialised from the env var at construction;
    /// cleared by @ref onDmabufRejected if the path proves unavailable at
    /// runtime, after which the session uses the pixel path.
    bool m_dmabufEnabled = false;
    /// Consecutive dma-buf capture failures (export or daemon rejection).
    /// Reset on success; triggers the session fallback at
    /// @ref DmabufFailureThreshold.
    int m_dmabufConsecutiveFailures = 0;

    /// Small pool of persistent FBO textures driving the dma-buf path.
    /// Consecutive captures round-robin across the pool so a texture isn't
    /// reused until @ref TexturePoolSize captures later — long enough for the
    /// daemon to have copied the exported buffer into its own per-candidate
    /// texture. The raw-pixel path does not use the pool (@ref grabWindowImage
    /// allocates a throwaway texture and copies immediately via toImage()).
    static constexpr int TexturePoolSize = 3;
    std::array<std::unique_ptr<KWin::GLTexture>, TexturePoolSize> m_texturePool;
    int m_poolNext = 0;

    QQueue<Pending> m_queue;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership via the set,
    /// O(1) oldest-first eviction via the queue. Kept strictly in sync.
    QSet<QUuid> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QVector>

#include <cstdint>
#include <memory>

class QImage;

namespace KWin {
class EffectWindow;
class GLTexture;
class RectF;
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
 * Two transports carry the result to the daemon.
 *
 * The DEFAULT is the zero-copy GPU path: the rendered FBO texture is
 * exported as a single-plane dma-buf and the fd shipped via
 * @c org.plasmazones.Overlay.setWindowThumbnailDmabuf, so the pixels never
 * cross the session bus. Captures on this path run as one batch per show,
 * paying the settle delay and the GL-context window once for every queued
 * candidate. Setting @c PLASMAZONES_DMABUF_THUMBNAILS=0 pins the session to
 * raw pixels outright.
 *
 * The FALLBACK is the raw-pixel path, taken automatically whenever the
 * dma-buf one cannot carry the frame — the driver lacks the required EGL
 * extensions, or the daemon repeatedly rejects the import
 * (@ref onDmabufRejected) — so a preview appears on every driver and RHI
 * backend. Here captures run sequentially, one render+readback at a time,
 * mirroring the throttling the daemon's previous ScreenShot2 path needed
 * (concurrent CaptureWindow calls could starve KWin's screenshot queue), and
 * each completed image is posted via
 * @c org.plasmazones.Overlay.setSnapAssistThumbnail as raw ARGB32
 * (non-premultiplied) bytes plus dimensions — no PNG encode, no base64. The
 * daemon validates the buffer shape and copies the bytes into a QImage that
 * lands in its bounded LRU cache.
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
     * Called when the daemon's cache is known to be empty: daemon restart /
     * service registration after a disconnect, and the daemon's idle-grace
     * cache trim (signalled via snapAssistThumbnailCacheTrimmed). Without
     * this the kwin-effect would keep skipping captures for handles the
     * daemon no longer holds, stranding snap-assist on icons until the LRU
     * window rolls past — and because @ref bumpRecency re-promotes skipped
     * handles, it would in practice never roll past.
     */
    void resetRecentlyPosted();

    /**
     * @brief Restore the dma-buf path after a daemon-ready transition.
     *
     * A daemon restart turns in-flight dma-buf posts into transport errors;
     * historically those latched the session onto the raw-pixel path with no
     * recovery edge. Called from the daemon-ready hook ONLY (not the cache
     * trim, which fires repeatedly in healthy sessions): re-enables the path
     * and zeroes the failure counter unless the env gate is off. No-op when
     * PLASMAZONES_DMABUF_THUMBNAILS=0.
     */
    void rearmDmabufPath();

private Q_SLOTS:
    void processNext();

private:
    /// Single-plane DMA-BUF exported from a rendered thumbnail texture. @c ok
    /// is false when the compositor is not on an EGL/GL backend or the driver
    /// lacks EGL_MESA_image_dma_buf_export. Per-export failures and daemon
    /// import rejections are counted; after @ref DmabufFailureThreshold
    /// consecutive failures the capture switches the whole session back to the
    /// raw-pixel path (@ref countDmabufFailure) so an unsupported setup
    /// degrades to working pixel thumbnails instead of stranding candidates
    /// on icons.
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
    /// Per-capture timing sample for the PLASMAZONES_THUMBNAIL_TRACE flag.
    /// Filled by the render functions (stage durations in microseconds, the
    /// fitted size) and completed by the post functions (payload bytes,
    /// D-Bus round-trip). Only populated when @ref m_traceEnabled; the
    /// render functions take it as an optional out-param so the untraced
    /// path pays nothing beyond a null check.
    struct TraceSample
    {
        QSize fitted;
        qint64 renderUs = 0; ///< drawWindow into the FBO (GPU submit, not completion).
        qint64 readbackUs = 0; ///< toImage + flip: includes the glReadPixels stall. Pixel path only.
        qint64 convertUs = 0; ///< premultiplied -> straight ARGB32. Pixel path only.
        qint64 exportUs = 0; ///< EGLImage + dma-buf export. dma-buf path only.
        qint64 packUs = 0; ///< memcpy into the D-Bus QByteArray. Pixel path only.
        qint64 payloadBytes = 0;
    };

    /// Render @p w into an offscreen GLFramebuffer fit within @p box (aspect
    /// ratio preserved) and read it back as a straight-alpha ARGB32 QImage.
    /// Returns a null image if the window can't be found/rendered. MUST run on
    /// the compositor thread; it makes the GL context current itself, so it is
    /// safe to call outside a paint pass (KWin 6.7's drawWindow/GLFramebuffer
    /// path needs no OutputFrame). This is the raw-pixel path.
    QImage grabWindowImage(KWin::EffectWindow* w, QSize box, TraceSample* trace = nullptr) const;

    /// Render @p w (blit-flipped to top-down orientation) into a FRESH
    /// GLFramebuffer texture and return it. Used by the dma-buf path: the
    /// exported buffer aliases the texture's storage, and the exported fd
    /// keeps that storage alive on its own, so the caller drops the texture
    /// right after @ref exportTextureToDmabuf. One buffer per export means
    /// no later capture can ever overwrite a buffer the daemon still
    /// displays, and the daemon can sample the import directly instead of
    /// copying it out of a shared slot. Returns nullptr if the window can't
    /// be found/rendered.
    ///
    /// Unlike @ref grabWindowImage this does NOT manage GL context currency:
    /// the caller makes the compositor context current before calling and
    /// keeps it current through the subsequent @ref exportTextureToDmabuf
    /// (which reads it via @c eglGetCurrentContext) and through the
    /// texture's destruction (glDeleteTextures needs the context too).
    ///
    /// On a nullptr return, @p candidateNotRenderable distinguishes the two
    /// failure domains the caller must treat differently: true = THIS
    /// candidate has nothing to render (degenerate geometry, sliver fit) —
    /// drop it without counting; false = a capability or resource failure
    /// (no blit support, allocation failure, incomplete FBO) — count it
    /// toward the session fallback so the pixel path can take over.
    /// Collapsing both into a bare nullptr let a blit-less driver drop
    /// every candidate forever with m_dmabufEnabled still true.
    std::unique_ptr<KWin::GLTexture> renderWindowToExportTexture(KWin::EffectWindow* w, QSize box,
                                                                 bool* candidateNotRenderable,
                                                                 TraceSample* trace = nullptr);

    void postThumbnail(const Pending& p, const QImage& image, TraceSample trace);
    /// @p generation is the queue generation the capture ran under; the
    /// rejection reply honours it the same way attemptCapture's timer lambda
    /// does — a stale rejection still counts toward the capability fallback
    /// but must not re-inject its candidate into a replaced queue.
    void postThumbnailDmabuf(const Pending& p, const DmabufExport& exported, int generation, TraceSample trace);

    /// Count one dma-buf capture failure toward the session fallback,
    /// flipping to the raw-pixel path at @ref DmabufFailureThreshold.
    /// Shared by onDmabufRejected (fresh failures, which also re-enqueue)
    /// and the stale-generation rejection path (count only).
    void countDmabufFailure();

    /// Record a dma-buf capture failure (export failure or daemon import
    /// rejection) for @p p. After @ref DmabufFailureThreshold consecutive
    /// failures, permanently switches this session to the raw-pixel path
    /// (clears @c m_dmabufEnabled) and re-enqueues @p p so it re-captures via
    /// pixels. Pure state mutation — the caller owns kicking @ref processNext.
    void onDmabufRejected(const Pending& p);

    /// Export a rendered thumbnail texture to a single-plane dma-buf via
    /// EGL_MESA_image_dma_buf_export. Must be called with KWin's GL/EGL
    /// context current (i.e. while @ref renderWindowToExportTexture's context
    /// is still active). Returns {ok=false} on any failure; the caller drops
    /// the candidate. Non-const: its whole purpose is external side effects
    /// (EGLImage + sync creation/destruction, fd duplication).
    DmabufExport exportTextureToDmabuf(KWin::GLTexture* texture);

    /// Render and read back the thumbnail for @p p; on an empty image (null
    /// OR fully transparent — a not-yet-renderable window draws nothing into
    /// the cleared FBO, which reads back valid), retry once with a longer
    /// delay before giving up. @p generation is compared against
    /// @ref m_queueGeneration when the timer fires so a capture queued
    /// before a captureCandidates replacement skips its stale render/post
    /// while still advancing the queue.
    void attemptCapture(const Pending& p, int delayMs, int retriesLeft, int generation);

    /// dma-buf counterpart of @ref attemptCapture, for a whole batch: one
    /// settle timer and one GL-context-current window cover every candidate
    /// in @p batch, so N candidates cost one delay rather than N and one
    /// makeCurrent rather than N. Each candidate is rendered into its own
    /// export texture and posted; the ones whose export failed are retried
    /// together after @ref RENDER_RETRY_MS while @p retriesLeft allows, and
    /// classified (drop vs. session-fallback count) once it runs out.
    /// @p generation semantics match attemptCapture: a stale batch advances
    /// the queue without rendering.
    void attemptDmabufBatch(const QVector<Pending>& batch, int delayMs, int retriesLeft, int generation);

    /// Aspect-preserving fit of @p wg into @p box, never upscaling (scale
    /// clamped to 1.0). Returns an empty size when either fitted axis lands
    /// below the minimum useful thumbnail size — the caller treats that as a
    /// capture failure (icon fallback) rather than shipping a sliver.
    static QSize fittedThumbnailSize(const KWin::RectF& wg, QSize box, qreal* scaleOut);

    /// Mark @p handle as posted to the daemon at capture box @p box,
    /// evicting the least-recently-used entry if the bookkeeping is at
    /// capacity. Called from the D-Bus pending-call success path inside
    /// @ref postThumbnail so failed sends leave the handle un-tracked and
    /// the next snap-assist invocation retries. Re-marking an
    /// already-tracked handle bumps it to the most-recently-used end
    /// (mirrors the daemon's QCache promote-on-insert) and keeps the
    /// larger of the two boxes.
    void markRecentlyPosted(const QUuid& handle, QSize box);

    /// True when @p handle was recently posted to the daemon at a box at
    /// least as large as @p box and is (probably) still resident in the
    /// daemon's bounded LRU cache. Mirrors the daemon's
    /// @c SnapAssistThumbnailProvider::CacheCapacity so this side stays in
    /// sync with the daemon's eviction window. The box comparison is what
    /// keeps a thumbnail captured small for a crowded show from being
    /// served upscaled when a later show draws it large: that show
    /// re-captures instead of skipping.
    bool wasRecentlyPosted(const QUuid& handle, QSize box) const;

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

    /// Zero-copy GPU path, on unless PLASMAZONES_DMABUF_THUMBNAILS=0. When
    /// set, each capture renders into its own FBO texture, exports it as a
    /// dma-buf and posts via setWindowThumbnailDmabuf instead of the raw-ARGB32
    /// setSnapAssistThumbnail. Initialised from the env var at construction;
    /// cleared by @ref countDmabufFailure if the path proves unavailable at
    /// runtime, after which the session uses the pixel path (until a
    /// daemon-ready transition re-arms it via @ref rearmDmabufPath).
    bool m_dmabufEnabled = false;
    /// PLASMAZONES_THUMBNAIL_TRACE: per-capture stage timings to the
    /// kwin.effect.plasmazones.snapassist.trace category. Process-constant.
    const bool m_traceEnabled = false;
    /// Consecutive dma-buf capture failures (export or daemon rejection).
    /// Reset on success; triggers the session fallback at
    /// @ref DmabufFailureThreshold.
    int m_dmabufConsecutiveFailures = 0;

    /// Scratch render target for the dma-buf path's orientation flip: the
    /// window renders here bottom-up (GL origin), then blits flipped into the
    /// export texture so the exported buffer is top-down. Reallocated on size
    /// change, freed in the destructor. The only GL object this class keeps
    /// between captures: export textures live exactly as long as their
    /// export (see renderWindowToExportTexture).
    std::unique_ptr<KWin::GLTexture> m_flipScratch;

    QQueue<Pending> m_queue;
    /// Bumped by every captureCandidates; in-flight capture timers carry the
    /// generation they were queued under and no-op (queue-advance only) when
    /// it no longer matches — see attemptCapture.
    int m_queueGeneration = 0;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership plus the
    /// posted box's major axis via the hash, O(1) oldest-first eviction via
    /// the queue. Kept strictly in sync.
    QHash<QUuid, int> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

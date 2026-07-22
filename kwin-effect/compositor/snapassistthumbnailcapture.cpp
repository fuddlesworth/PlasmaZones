// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

// epoxy MUST precede any other GL/EGL include so it can interpose the
// function pointers. It also pulls in the EGL/GL types and the
// EGL_MESA_image_dma_buf_export entry points used by exportTextureToDmabuf.
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>

#include <cstring>
#include <unistd.h>

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QImage>
#include <QLoggingCategory>
#include <QPoint>
#include <QTimer>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSnapAssistCapture, "kwin.effect.plasmazones.snapassist.capture", QtWarningMsg)

namespace PlasmaZones {

namespace {
/// Settle delay before the first capture attempt for a candidate. A freshly
/// mapped window may not have a renderable compositor frame the instant it is
/// queued; one frame at 60Hz (~16ms) is reliably enough for drawWindow to read
/// non-empty content, while not adding meaningful latency to the snap-assist UI
/// (which already shows icons immediately and fades thumbs in asynchronously).
constexpr int RENDER_SETTLE_MS = 16;
/// Retry delay used when the first render produced an empty buffer. Four
/// frames at 60Hz: long enough for a stalled compositor frame to clear,
/// short enough that the user still sees the thumbnail before the eye
/// notices the fallback icon.
constexpr int RENDER_RETRY_MS = 64;
/// Consecutive dma-buf capture failures (export failure or daemon import
/// rejection) before the session permanently falls back to the raw-pixel path.
/// >1 so a single transient bad frame doesn't disable the zero-copy path,
/// while a genuine capability gap (every frame fails) trips it quickly.
constexpr int DmabufFailureThreshold = 2;
} // namespace

SnapAssistThumbnailCapture::SnapAssistThumbnailCapture(QObject* parent)
    : QObject(parent)
    , m_dmabufEnabled(qEnvironmentVariableIsSet("PLASMAZONES_DMABUF_THUMBNAILS"))
{
    if (m_dmabufEnabled) {
        qCInfo(lcSnapAssistCapture) << "PLASMAZONES_DMABUF_THUMBNAILS set — snap-assist thumbnails will be exported "
                                       "as dma-bufs (experimental zero-copy path).";
    }
}

SnapAssistThumbnailCapture::~SnapAssistThumbnailCapture() = default;

void SnapAssistThumbnailCapture::captureCandidates(const QVector<Candidate>& candidates, QSize maxSize)
{
    // Replace any pending queue from a prior snap-assist invocation. Capture
    // is bounded by the daemon's QCache LRU; we additionally skip handles we
    // posted recently, on the assumption the daemon still holds them. The
    // assumption is allowed to be wrong — see comment on @c RecentPostedCapacity
    // for the bounded failure mode and @ref resetRecentlyPosted for the
    // daemon-restart escape hatch.
    m_queue.clear();
    int skipped = 0;
    for (const auto& c : candidates) {
        if (c.internalId.isNull()) {
            continue;
        }
        if (wasRecentlyPosted(c.internalId)) {
            // Promote: the handle is being "used" via the skip-recapture
            // decision, mirroring the daemon's QCache promote-on-access.
            // Without this, a frequently re-snapped window FIFOs out of
            // the bookkeeping window even though the daemon keeps it MRU
            // and never evicts it.
            bumpRecency(c.internalId);
            ++skipped;
            continue;
        }
        m_queue.enqueue({c.internalId, maxSize});
    }
    if (skipped > 0) {
        // Info-level: this is the visible signal that the recently-posted
        // dedup window is doing its job (or, if it ever skips when the
        // daemon's cache is cold, that the cap-drift fallback path described
        // on RecentPostedCapacity has kicked in). Without this at info, an
        // operator chasing a "thumbnails not appearing" report has to
        // enable kwin.effect.plasmazones.* debug logs first.
        qCInfo(lcSnapAssistCapture) << "captureCandidates: skipped" << skipped << "recently-posted of"
                                    << candidates.size() << "; queued=" << m_queue.size();
    }
    if (m_queue.isEmpty() || m_busy) {
        return;
    }
    QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
}

void SnapAssistThumbnailCapture::resetRecentlyPosted()
{
    if (m_recentlyPostedSet.isEmpty() && m_recentlyPostedOrder.isEmpty()) {
        return;
    }
    // Info-level: drops happen at daemon-ready transitions only, so they're
    // rare and meaningful (daemon restart / first registration). An operator
    // diagnosing "snap-assist thumbnails are blank after restart" should see
    // this in the default-warning log rules.
    qCInfo(lcSnapAssistCapture) << "resetRecentlyPosted: dropping" << m_recentlyPostedSet.size()
                                << "tracked handles (daemon cache assumed empty)";
    m_recentlyPostedSet.clear();
    m_recentlyPostedOrder.clear();
}

bool SnapAssistThumbnailCapture::wasRecentlyPosted(const QUuid& handle) const
{
    return m_recentlyPostedSet.contains(handle);
}

void SnapAssistThumbnailCapture::markRecentlyPosted(const QUuid& handle)
{
    if (m_recentlyPostedSet.contains(handle)) {
        // Re-mark: bump to MRU so a re-posted handle (which the daemon
        // also just promoted via QCache::insert) stays at the head of
        // the order queue. The previous "leave queue position alone"
        // behaviour drifted re-posted handles out of the dedup window
        // in first-post order even though the daemon would never evict
        // them, causing wasted re-captures.
        bumpRecency(handle);
        return;
    }
    m_recentlyPostedSet.insert(handle);
    m_recentlyPostedOrder.enqueue(handle);
    while (m_recentlyPostedOrder.size() > RecentPostedCapacity) {
        const QUuid evicted = m_recentlyPostedOrder.dequeue();
        m_recentlyPostedSet.remove(evicted);
    }
}

void SnapAssistThumbnailCapture::bumpRecency(const QUuid& handle)
{
    // Debug-build invariant guard: every caller (the @ref captureCandidates
    // skip-recapture path and @ref markRecentlyPosted's re-mark branch) only
    // reaches here after observing @c m_recentlyPostedSet.contains(handle).
    // If the queue diverges from the set the set/queue size invariant has
    // already broken — surface that early in debug rather than silently
    // no-op'ing here. Compiles to nothing in release.
    Q_ASSERT_X(m_recentlyPostedSet.contains(handle), "bumpRecency",
               "called for a handle not in the set — set/queue invariant broken");
    // QQueue::removeOne is O(n) over n=RecentPostedCapacity; trivially
    // cheap for this cadence. Returns false if @p handle isn't present —
    // in that case skip the enqueue, otherwise we'd add a duplicate and
    // break the set/queue size invariant. (In debug builds the assert above
    // catches this earlier; the runtime guard stays for release safety.)
    if (m_recentlyPostedOrder.removeOne(handle)) {
        m_recentlyPostedOrder.enqueue(handle);
    }
}

QImage SnapAssistThumbnailCapture::grabWindowImage(KWin::EffectWindow* w, QSize box) const
{
    if (!w || box.isEmpty() || !KWin::effects) {
        return {};
    }
    // KWin 6.7: frameGeometry() is a KWin::RectF.
    const KWin::RectF wg = w->frameGeometry();
    if (wg.width() <= 0 || wg.height() <= 0) {
        return {};
    }
    // Fit the window into the bounding box, preserving aspect ratio. The FBO is
    // sized to the fitted content (not the full box) so the readback carries no
    // letterbox padding — the daemon stores the image at whatever size we ship.
    const qreal scale = qMin(qreal(box.width()) / wg.width(), qreal(box.height()) / wg.height());
    const QSize fbSize(qMax(1, qRound(wg.width() * scale)), qMax(1, qRound(wg.height() * scale)));

    // drawWindow() and the GLFramebuffer path issue raw GL, so the compositor
    // EGL context must be current. We run timer-driven, outside a paint pass, so
    // make it current ourselves (KWin 6.7's manual-FBO render needs no
    // OutputFrame). Non-OpenGL backends (software/QPainter compositing) return
    // false — snap-assist then falls back to icons.
    if (!KWin::effects->makeOpenGLContextCurrent()) {
        return {};
    }

    QImage result;
    auto texture = KWin::GLTexture::allocate(GL_RGBA8, fbSize);
    if (texture) {
        KWin::GLFramebuffer fbo(texture.get());
        KWin::RenderTarget renderTarget(&fbo);
        // renderRect = the window's logical geometry; scale maps it onto the
        // fbSize device target so the window fills the FBO at thumbnail size.
        KWin::RenderViewport viewport(wg, scale, renderTarget, QPoint());

        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        KWin::WindowPaintData data;
        // Route through effects->drawWindow (the same entry the effect's own
        // paintWindow uses) so the full draw chain renders the window's live
        // texture into our bound FBO. infinite() repaints the whole target.
        KWin::effects->drawWindow(renderTarget, viewport, w, KWin::Effect::PAINT_WINDOW_TRANSFORMED,
                                  KWin::Region::infinite(), data);
        KWin::GLFramebuffer::popFramebuffer();

        // toImage() yields Format_RGBA8888_Premultiplied; GL's framebuffer
        // origin is bottom-left, so flip to a top-down QImage.
        result = texture->toImage().flipped(Qt::Vertical);
    }
    KWin::effects->doneOpenGLContextCurrent();

    if (result.isNull()) {
        return {};
    }
    // Ship plain (straight-alpha) ARGB32 so the raw bytes match the daemon's
    // storage format and semi-transparent edges aren't darkened by an
    // unintended premultiplied composite at the image-provider boundary.
    return result.convertToFormat(QImage::Format_ARGB32);
}

KWin::GLTexture* SnapAssistThumbnailCapture::renderWindowToPooledTexture(KWin::EffectWindow* w, QSize box)
{
    // Caller guarantees the compositor GL/EGL context is current (the dma-buf
    // export that follows reads it via eglGetCurrentContext), so unlike
    // grabWindowImage this neither makes the context current nor releases it.
    if (!w || box.isEmpty() || !KWin::effects) {
        return nullptr;
    }
    const KWin::RectF wg = w->frameGeometry();
    if (wg.width() <= 0 || wg.height() <= 0) {
        return nullptr;
    }
    const qreal scale = qMin(qreal(box.width()) / wg.width(), qreal(box.height()) / wg.height());
    const QSize fbSize(qMax(1, qRound(wg.width() * scale)), qMax(1, qRound(wg.height() * scale)));

    // Round-robin a small pool of persistent textures: the exported dma-buf
    // aliases the slot's texture, so it must outlive the daemon's async import.
    // Reusing a slot only TexturePoolSize captures later gives that copy margin
    // (mirrors the producer-scene pool the OffscreenQuickScene path used).
    std::unique_ptr<KWin::GLTexture>& slot = m_texturePool[m_poolNext];
    if (!slot || slot->size() != fbSize) {
        slot = KWin::GLTexture::allocate(GL_RGBA8, fbSize);
    }
    if (!slot) {
        return nullptr;
    }
    KWin::GLTexture* texture = slot.get();

    KWin::GLFramebuffer fbo(texture);
    KWin::RenderTarget renderTarget(&fbo);
    KWin::RenderViewport viewport(wg, scale, renderTarget, QPoint());

    KWin::GLFramebuffer::pushFramebuffer(&fbo);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    KWin::WindowPaintData data;
    KWin::effects->drawWindow(renderTarget, viewport, w, KWin::Effect::PAINT_WINDOW_TRANSFORMED,
                              KWin::Region::infinite(), data);
    KWin::GLFramebuffer::popFramebuffer();

    m_poolNext = (m_poolNext + 1) % TexturePoolSize;
    return texture;
}

void SnapAssistThumbnailCapture::processNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    Pending p = m_queue.dequeue();
    attemptCapture(p, RENDER_SETTLE_MS, /*retriesLeft=*/1);
}

void SnapAssistThumbnailCapture::attemptCapture(Pending p, int delayMs, int retriesLeft)
{
    QTimer::singleShot(delayMs, this, [this, p, retriesLeft]() {
        // Resolve the EffectWindow fresh each attempt: the candidate may have
        // closed between queueing and firing, in which case findWindow returns
        // null and the render yields nothing (handled below).
        KWin::EffectWindow* w = KWin::effects ? KWin::effects->findWindow(p.internalId) : nullptr;

        if (m_dmabufEnabled) {
            // Zero-copy path: render the window into a pooled FBO texture and
            // export it as a dma-buf. The render and the EGL export must share
            // one context-current window — renderWindowToPooledTexture assumes
            // the context is already current and exportTextureToDmabuf reads it
            // via eglGetCurrentContext — so currency is managed here, around
            // both.
            DmabufExport exported;
            if (w && KWin::effects && KWin::effects->makeOpenGLContextCurrent()) {
                KWin::GLTexture* texture = renderWindowToPooledTexture(w, p.maxSize);
                if (texture) {
                    exported = exportTextureToDmabuf(texture);
                }
                KWin::effects->doneOpenGLContextCurrent();
            }
            if (!exported.ok && retriesLeft > 0) {
                attemptCapture(p, RENDER_RETRY_MS, retriesLeft - 1);
                return;
            }
            if (exported.ok) {
                postThumbnailDmabuf(p, exported);
            } else {
                qCDebug(lcSnapAssistCapture)
                    << "captureCandidates:" << p.internalId.toString() << "dma-buf export failed after retry";
                // Count export failures toward the session fallback (may
                // re-enqueue p in pixel mode); the trailing kick below advances
                // the queue and picks up any re-enqueued candidate.
                onDmabufRejected(p);
            }
            // Advance the queue now — the dma-buf post (if any) is async; we do
            // not wait for its reply to start the next capture. The reply lambda
            // has its own guarded kick (only fires when idle) solely to drain a
            // candidate that onDmabufRejected re-enqueued after the queue had
            // already emptied. processNext is idempotent on an empty queue, so
            // the two kick sites never double-dispatch real work.
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }

        QImage image = grabWindowImage(w, p.maxSize);
        if (image.isNull() && retriesLeft > 0) {
            // The window's first compositor frame after mapping is occasionally
            // not yet renderable. Wait one more frame interval — same Pending,
            // no queue shuffle — before giving up.
            attemptCapture(p, RENDER_RETRY_MS, retriesLeft - 1);
            return;
        }
        if (!image.isNull()) {
            // Mark-recently-posted is deferred into the D-Bus success
            // callback inside @ref postThumbnail — see that function for
            // why "we sent it" isn't strong enough to claim the daemon
            // holds the entry.
            postThumbnail(p.internalId, image);
        } else {
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                         << "produced empty image after retry";
        }
        QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
    });
}

void SnapAssistThumbnailCapture::postThumbnail(const QUuid& internalId, const QImage& image)
{
    // Image is already Format_ARGB32 by the caller. Pack tight (no row
    // padding) so the daemon can reconstruct via QImage(uchar*, w, h,
    // bytesPerLine=w*4, Format_ARGB32) without having to communicate the
    // stride. Qt's internal stride for Format_ARGB32 is naturally 4-aligned
    // and almost always equals width*4; on that fast-path one bulk memcpy
    // beats a per-row loop. Fall back to the row loop only when Qt has
    // padded the stride (rare — typically only for non-aligned widths).
    const int width = image.width();
    const int height = image.height();
    const qsizetype rowBytes = qsizetype(width) * 4;
    QByteArray pixels;
    pixels.resize(rowBytes * height);
    char* dst = pixels.data();
    if (image.bytesPerLine() == rowBytes) {
        std::memcpy(dst, image.constBits(), rowBytes * height);
    } else {
        for (int y = 0; y < height; ++y) {
            std::memcpy(dst + y * rowBytes, image.constScanLine(y), rowBytes);
        }
    }

    const QString compositorHandle = internalId.toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setSnapAssistThumbnail"));
    msg << compositorHandle << width << height << pixels;

    // Bound watcher accumulation if the daemon's main thread wedges. The
    // post is genuinely async — `SnapAssistThumbnailPostTimeoutMs` is
    // "definitely something is wrong, drop the watcher" rather than a
    // meaningful expected latency. Without an explicit timeout the
    // kwin-effect could otherwise leak a watcher per snap-assist candidate
    // per show until Qt's default 25 s timeout expires, which under daemon
    // stress turns a transient hang into accumulated compositor-process
    // state.
    QDBusPendingCall pending =
        QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SnapAssistThumbnailPostTimeoutMs);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    // Capture @c this — the connect's context arg auto-disconnects the
    // lambda if `this` dies, so capture-by-pointer is safe across the
    // call round-trip. Mark-recently-posted is gated on the daemon's
    // explicit `accepted` reply, NOT just on transport success. The
    // daemon's slot is `bool`-returning and replies @c false on every
    // silent rejection path (auth failure, oversize-payload cap,
    // dimension/byte-count mismatch, post-shutdown engine teardown) —
    // treating those as success would mark the handle in the dedup
    // window, skip the next capture, and strand snap-assist on icons
    // until the FIFO rolls past.
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                     [this, internalId, compositorHandle](QDBusPendingCallWatcher* w) {
                         w->deleteLater();
                         QDBusPendingReply<bool> reply = *w;
                         if (reply.isError()) {
                             qCDebug(lcSnapAssistCapture) << "setSnapAssistThumbnail D-Bus call failed for"
                                                          << compositorHandle << ":" << reply.error().message();
                             return;
                         }
                         if (!reply.value()) {
                             qCDebug(lcSnapAssistCapture)
                                 << "setSnapAssistThumbnail rejected by daemon for" << compositorHandle
                                 << "— leaving handle out of recently-posted set so the next snap-assist re-captures.";
                             return;
                         }
                         markRecentlyPosted(internalId);
                     });
}

SnapAssistThumbnailCapture::DmabufExport
SnapAssistThumbnailCapture::exportTextureToDmabuf(KWin::GLTexture* texture) const
{
    DmabufExport result;
    if (!texture) {
        return result;
    }
    // EGL display/context of the compositor's current GL backend. EGL_NO_*
    // means KWin is not on an EGL/GL backend (e.g. a future Vulkan compositor
    // backend) — there is nothing to export here, so the caller drops the
    // candidate and snap-assist shows its icon.
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: no current EGL context (non-EGL backend)";
        return result;
    }
    // Verify the driver actually provides the EGL entry points this path calls
    // BEFORE calling any of them. With libepoxy a missing entry point resolves
    // to a stub that abort()s the process rather than returning an error, so
    // without these guards a driver lacking dma-buf export or native-fence sync
    // would CRASH the compositor the first time the env-gated path runs —
    // instead of returning {ok=false} and taking the raw-pixel fallback the
    // comments below rely on.
    if (!epoxy_has_egl_extension(dpy, "EGL_KHR_image_base")
        || !epoxy_has_egl_extension(dpy, "EGL_MESA_image_dma_buf_export")
        || !epoxy_has_egl_extension(dpy, "EGL_KHR_fence_sync")
        || !epoxy_has_egl_extension(dpy, "EGL_ANDROID_native_fence_sync")) {
        qCDebug(lcSnapAssistCapture)
            << "exportTextureToDmabuf: required EGL dma-buf/fence extensions unavailable — using raw-pixel path";
        return result;
    }
    if (texture->target() != GL_TEXTURE_2D) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: unexpected texture target" << texture->target();
        return result;
    }
    if (texture->size().isEmpty()) {
        // A zero-sized FBO texture would export a w=0/h=0 buffer the daemon is
        // guaranteed to reject — fail early (routes to retry, then fallback)
        // rather than allocate and ship an EGLImage + fd + fence for nothing.
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: zero-sized texture" << texture->size();
        return result;
    }
    // Wrap the GL texture as an EGLImage, then export its backing dma-buf.
    const EGLImageKHR image =
        eglCreateImageKHR(dpy, ctx, EGL_GL_TEXTURE_2D_KHR,
                          reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture->texture())), nullptr);
    if (image == EGL_NO_IMAGE_KHR) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglCreateImageKHR failed";
        return result;
    }
    int fourcc = 0;
    int numPlanes = 0;
    EGLuint64KHR modifier = 0;
    if (!eglExportDMABUFImageQueryMESA(dpy, image, &fourcc, &numPlanes, &modifier) || numPlanes != 1) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: query failed or unsupported plane count" << numPlanes;
        eglDestroyImageKHR(dpy, image);
        return result;
    }
    int fd = -1;
    EGLint stride = 0;
    EGLint offset = 0;
    const bool exported = eglExportDMABUFImageMESA(dpy, image, &fd, &stride, &offset);
    // The exported fd holds its own reference to the underlying buffer, so it
    // outlives the EGLImage — destroy the image regardless of success.
    eglDestroyImageKHR(dpy, image);
    if (!exported || fd < 0) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglExportDMABUFImageMESA failed";
        if (fd >= 0) {
            ::close(fd);
        }
        return result;
    }
    // Mandatory render-completion fence for the dma-buf path: a sync_file that
    // signals when the GL render into this buffer finishes, so the daemon waits
    // before sampling (correctness under repeated/live capture). If the driver
    // can't produce one, fail the export — the capability fallback then switches
    // the session to the raw-pixel path.
    //
    // Ordering: renderWindowToPooledTexture (issued earlier this call) renders
    // into this texture on the current GL context's command stream. eglCreateSync
    // with EGL_SYNC_NATIVE_FENCE_ANDROID inserts the fence into that same
    // stream AFTER those render commands, then glFlush() flushes the stream to
    // the GPU so the fence is schedulable and eglDupNativeFenceFDANDROID can
    // return a real sync_file. The fence therefore signals only once the render
    // it follows has completed — exactly the guarantee the daemon relies on.
    const EGLSyncKHR sync = eglCreateSyncKHR(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    if (sync == EGL_NO_SYNC_KHR) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: native fence sync unavailable";
        ::close(fd);
        return result;
    }
    glFlush(); // flush the stream (render + fence) to the GPU so the fence can signal
    const int fenceFd = eglDupNativeFenceFDANDROID(dpy, sync);
    eglDestroySyncKHR(dpy, sync);
    if (fenceFd < 0) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: eglDupNativeFenceFDANDROID failed";
        ::close(fd);
        return result;
    }
    result.ok = true;
    result.fd = fd;
    result.fenceFd = fenceFd;
    result.width = texture->size().width();
    result.height = texture->size().height();
    result.fourcc = static_cast<uint32_t>(fourcc);
    result.modifier = static_cast<uint64_t>(modifier);
    result.stride = static_cast<uint32_t>(stride);
    result.offset = static_cast<uint32_t>(offset);
    return result;
}

void SnapAssistThumbnailCapture::postThumbnailDmabuf(const Pending& p, const DmabufExport& exported)
{
    // The exported dma-buf aliases the pooled FBO texture, so it ships with a
    // render-completion fence (exported.fenceFd): the daemon waits on it before
    // sampling, which makes repeated/live capture correct rather than relying on
    // D-Bus round-trip latency outrunning the GPU. The texture pool
    // (renderWindowToPooledTexture) gives the daemon's copy-on-import a margin
    // before a slot is reused.
    const QString compositorHandle = p.internalId.toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setWindowThumbnailDmabuf"));
    // QDBusUnixFileDescriptor dup()s each fd in its constructor; we close our
    // originals after queuing the call.
    msg << compositorHandle << exported.width << exported.height << static_cast<uint>(exported.fourcc)
        << static_cast<qulonglong>(exported.modifier) << static_cast<uint>(exported.stride)
        << static_cast<uint>(exported.offset) << QVariant::fromValue(QDBusUnixFileDescriptor(exported.fd))
        << QVariant::fromValue(QDBusUnixFileDescriptor(exported.fenceFd));

    QDBusPendingCall pending =
        QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SnapAssistThumbnailPostTimeoutMs);
    ::close(exported.fd);
    ::close(exported.fenceFd);

    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    // Same accepted-gated recently-posted contract as the raw-pixel path: only
    // mark the handle when the daemon confirms it imported+stored the buffer.
    // A rejection (or transport error) feeds onDmabufRejected, which counts
    // toward the session fallback to the raw-pixel path.
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, p](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QString handle = p.internalId.toString();
        QDBusPendingReply<bool> reply = *w;
        if (reply.isError()) {
            qCDebug(lcSnapAssistCapture) << "setWindowThumbnailDmabuf D-Bus call failed for" << handle << ":"
                                         << reply.error().message();
            onDmabufRejected(p);
        } else if (!reply.value()) {
            qCDebug(lcSnapAssistCapture) << "setWindowThumbnailDmabuf rejected by daemon for" << handle;
            onDmabufRejected(p);
        } else {
            m_dmabufConsecutiveFailures = 0;
            markRecentlyPosted(p.internalId);
        }
        // onDmabufRejected may have re-enqueued p (pixel-mode re-capture); if no
        // capture is in flight, kick the queue so it isn't left stranded.
        if (!m_busy && !m_queue.isEmpty()) {
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
        }
    });
}

void SnapAssistThumbnailCapture::onDmabufRejected(const Pending& p)
{
    if (!m_dmabufEnabled) {
        return; // already fell back to the pixel path this session
    }
    ++m_dmabufConsecutiveFailures;
    if (m_dmabufConsecutiveFailures < DmabufFailureThreshold) {
        qCDebug(lcSnapAssistCapture) << "dma-buf capture failed (" << m_dmabufConsecutiveFailures
                                     << "consecutive) — will retry dma-buf on the next candidate";
        return;
    }
    qCWarning(lcSnapAssistCapture)
        << "dma-buf thumbnails repeatedly unavailable (export or daemon import failing) — falling back to "
           "raw-pixel thumbnails for the rest of this session.";
    m_dmabufEnabled = false;
    m_dmabufConsecutiveFailures = 0;
    // The mode flip is picked up by the next attemptCapture, which routes
    // through the raw-pixel grabWindowImage path. The texture pool simply goes
    // unused from here (freed at destruction) — the pixel path allocates its
    // own throwaway texture per capture.
    m_queue.enqueue(p); // re-capture this candidate via the pixel path
}

} // namespace PlasmaZones

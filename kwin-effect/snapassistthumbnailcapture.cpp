// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

// epoxy MUST precede any other GL/EGL include so it can interpose the
// function pointers. It also pulls in the EGL/GL types and the
// EGL_MESA_image_dma_buf_export entry points used by exportTextureToDmabuf.
#include <epoxy/gl.h>
#include <epoxy/egl.h>

#include <effect/offscreenquickview.h>
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
#include <QQuickItem>
#include <QSizeF>
#include <QTimer>
#include <QUrl>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSnapAssistCapture, "kwin.effect.plasmazones.snapassist.capture", QtWarningMsg)

namespace PlasmaZones {

namespace {
/// Render-settle delay after binding @c wId to a fresh window. WindowThumbnail
/// has to look up the EffectWindow, attach to its surface, and post a frame —
/// none of that is synchronous with the property write. A single frame at
/// 60Hz (~16ms) is reliably enough for KWin's surface->thumbnail pipeline to
/// produce non-empty output, while not adding meaningful latency to the
/// snap-assist UI (which already shows icons immediately and fades thumbs in
/// asynchronously).
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

QUrl thumbnailQmlUrl()
{
    return QUrl(QStringLiteral("qrc:/plasmazones-effect/qml/SnapAssistThumb.qml"));
}
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
    // QQueue::removeOne is O(n) over n=RecentPostedCapacity (24); trivially
    // cheap for this cadence. Returns false if @p handle isn't present —
    // in that case skip the enqueue, otherwise we'd add a duplicate and
    // break the set/queue size invariant. (In debug builds the assert above
    // catches this earlier; the runtime guard stays for release safety.)
    if (m_recentlyPostedOrder.removeOne(handle)) {
        m_recentlyPostedOrder.enqueue(handle);
    }
}

void SnapAssistThumbnailCapture::dropQueueAndIdle()
{
    m_queue.clear();
    m_busy = false;
}

void SnapAssistThumbnailCapture::ensureScene()
{
    if (m_scene) {
        return;
    }
    // ExportMode::Image: bufferAsImage() returns a usable QImage after
    // each render. Leave visibility at its default (true) — OffscreenQuickView's
    // setVisible(false) calls releaseResources() on the underlying QQuickWindow,
    // which tears down the scene graph and stops rendering. With visible=false
    // m_scene->update() would no-op and bufferAsImage() returns null, so every
    // candidate would post-empty and snap-assist would strand on icons. The
    // window is offscreen by construction — there's no wl_surface mapping to
    // worry about — so visible=true here just keeps the QSGRenderContext live
    // for the WindowThumbnail QSGTextureNode to render against.
    //
    // Disable automatic repaint — we drive update() explicitly once per
    // capture so we know exactly which frame we're reading back.
    // Texture export mode for the dma-buf path (bufferAsTexture); Image mode
    // for the raw-pixel path (bufferAsImage). The two are mutually exclusive
    // on one scene — Texture mode leaves bufferAsImage blank — so the mode is
    // fixed by the gate latched at construction.
    const auto exportMode =
        m_dmabufEnabled ? KWin::OffscreenQuickView::ExportMode::Texture : KWin::OffscreenQuickView::ExportMode::Image;
    m_scene = std::make_unique<KWin::OffscreenQuickScene>(exportMode);
    m_scene->setAutomaticRepaint(false);
    const QUrl url = thumbnailQmlUrl();
    m_scene->setSource(url);
    if (!m_scene->rootItem()) {
        qCWarning(lcSnapAssistCapture) << "Failed to load thumbnail QML scene from" << url.toString();
    }
}

void SnapAssistThumbnailCapture::processNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        return;
    }
    ensureScene();
    if (!m_scene || !m_scene->rootItem()) {
        // Scene didn't load — abort the rest of the queue rather than spin
        // forever on a broken QML resource. Snap-assist falls back to icons.
        // Validate before dequeue so a still-queued handle isn't silently
        // dropped past the @ref qCWarning that names the failing resource.
        qCWarning(lcSnapAssistCapture) << "processNext: scene unavailable — dropping" << m_queue.size()
                                       << "queued capture(s); snap-assist will fall back to icons.";
        dropQueueAndIdle();
        return;
    }
    m_busy = true;
    Pending p = m_queue.dequeue();

    QQuickItem* root = m_scene->rootItem();
    // WindowThumbnail's @c wId is declared @c QUuid; we pass the unbraced
    // string form so the QML side accepts a typed @c property string and
    // the Qt property bridge converts to QUuid once at the kwin boundary.
    root->setProperty("wId", p.internalId.toString(QUuid::WithoutBraces));
    root->setProperty("boxSize", QVariant::fromValue(QSizeF(p.maxSize)));
    m_scene->setGeometry(QRect(QPoint(0, 0), p.maxSize));

    attemptCapture(p, RENDER_SETTLE_MS, /*retriesLeft=*/1);
}

void SnapAssistThumbnailCapture::attemptCapture(Pending p, int delayMs, int retriesLeft)
{
    QTimer::singleShot(delayMs, this, [this, p, retriesLeft]() {
        // Bail out if the scene was torn down between schedule and fire (Qt
        // resource teardown during shutdown), or if it lost its root item
        // (the QML scene failed to reload after a hot-resource update).
        // Either way, drop @c m_busy and re-enter the queue loop so a future
        // captureCandidates() can dispatch fresh work — leaving m_busy=true
        // would silently wedge the queue forever.
        if (!m_scene || !m_scene->rootItem()) {
            qCDebug(lcSnapAssistCapture) << "attemptCapture: scene torn down — aborting" << p.internalId.toString();
            dropQueueAndIdle();
            // No follow-up @c processNext kick: @c dropQueueAndIdle has
            // already cleared the queue, so re-entering would be a no-op
            // on an empty queue. The next @ref captureCandidates call
            // will re-dispatch from scratch (and will hit the same
            // scene-torn-down path inside @ref processNext if the scene
            // never recovers, which logs at warning and discards cleanly).
            return;
        }
        m_scene->update();

        if (m_dmabufEnabled) {
            // Zero-copy path: export the freshly-rendered FBO texture as a
            // dma-buf and hand the fd to the daemon. bufferAsTexture() requires
            // KWin's GL context to be current, which update() above guarantees.
            KWin::GLTexture* texture = m_scene->bufferAsTexture();
            const DmabufExport exported = texture ? exportTextureToDmabuf(texture) : DmabufExport{};
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
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }

        QImage image = m_scene->bufferAsImage();
        if (image.isNull() && retriesLeft > 0) {
            // Compositor occasionally drops the first frame after wId is
            // bound. Wait one more frame interval — same Pending, no queue
            // shuffle — before giving up.
            attemptCapture(p, RENDER_RETRY_MS, retriesLeft - 1);
            return;
        }
        if (!image.isNull()) {
            // bufferAsImage commonly returns Format_ARGB32_Premultiplied
            // (matches the FBO layout). Convert to plain ARGB32 so the
            // raw bytes we ship match the daemon's storage format and
            // semi-transparent edges aren't darkened by an unintended
            // premultiplied composite at the QML provider boundary.
            if (image.format() != QImage::Format_ARGB32) {
                image = image.convertToFormat(QImage::Format_ARGB32);
            }
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
    if (texture->target() != GL_TEXTURE_2D) {
        qCDebug(lcSnapAssistCapture) << "exportTextureToDmabuf: unexpected texture target" << texture->target();
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
    result.ok = true;
    result.fd = fd;
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
    // SPIKE SYNC CAVEAT: the exported dma-buf aliases KWin's reused
    // OffscreenQuickScene FBO — there is no fence yet. Captures are sequential
    // (one in flight at a time) and the daemon imports before the next
    // capture's update() overwrites the buffer; the D-Bus round-trip latency
    // dwarfs the GPU import, so the race window is negligible. Explicit fences
    // + a buffer pool are a later increment.
    const QString compositorHandle = p.internalId.toString();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setSnapAssistThumbnailDmabuf"));
    // QDBusUnixFileDescriptor dup()s the fd in its constructor; we close our
    // original after queuing the call.
    msg << compositorHandle << exported.width << exported.height << static_cast<uint>(exported.fourcc)
        << static_cast<qulonglong>(exported.modifier) << static_cast<uint>(exported.stride)
        << static_cast<uint>(exported.offset) << QVariant::fromValue(QDBusUnixFileDescriptor(exported.fd));

    QDBusPendingCall pending =
        QDBusConnection::sessionBus().asyncCall(msg, PhosphorProtocol::Service::SnapAssistThumbnailPostTimeoutMs);
    ::close(exported.fd);

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
            qCDebug(lcSnapAssistCapture) << "setSnapAssistThumbnailDmabuf D-Bus call failed for" << handle << ":"
                                         << reply.error().message();
            onDmabufRejected(p);
        } else if (!reply.value()) {
            qCDebug(lcSnapAssistCapture) << "setSnapAssistThumbnailDmabuf rejected by daemon for" << handle;
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
    m_scene.reset(); // ensureScene() rebuilds in ExportMode::Image
    m_queue.enqueue(p); // re-capture this candidate via the pixel path
}

} // namespace PlasmaZones

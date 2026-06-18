// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <core/region.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>

#include <cstring>

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QImage>
#include <QLoggingCategory>
#include <QPoint>
#include <QTimer>
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
} // namespace

SnapAssistThumbnailCapture::SnapAssistThumbnailCapture(QObject* parent)
    : QObject(parent)
{
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
        // null and grabWindowImage yields a null image (handled below).
        KWin::EffectWindow* w = KWin::effects ? KWin::effects->findWindow(p.internalId) : nullptr;
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

} // namespace PlasmaZones

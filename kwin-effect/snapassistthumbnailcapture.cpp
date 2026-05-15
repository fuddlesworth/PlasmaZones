// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/offscreenquickview.h>

#include <cstring>

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
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

QUrl thumbnailQmlUrl()
{
    return QUrl(QStringLiteral("qrc:/plasmazones-effect/qml/SnapAssistThumb.qml"));
}
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
    m_scene = std::make_unique<KWin::OffscreenQuickScene>(KWin::OffscreenQuickView::ExportMode::Image);
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

} // namespace PlasmaZones

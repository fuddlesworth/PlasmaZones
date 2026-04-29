// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/offscreenquickview.h>

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QImage>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QSizeF>
#include <QTimer>
#include <QUrl>

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
constexpr int kRenderSettleMs = 16;
/// Retry delay used when the first render produced an empty buffer. Four
/// frames at 60Hz: long enough for a stalled compositor frame to clear,
/// short enough that the user still sees the thumbnail before the eye
/// notices the fallback icon.
constexpr int kRenderRetryMs = 64;

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
    // assumption is allowed to be wrong — see comment on @c kRecentPostedCapacity
    // for the bounded failure mode.
    m_queue.clear();
    int skipped = 0;
    for (const auto& c : candidates) {
        if (c.internalId.isNull()) {
            continue;
        }
        if (wasRecentlyPosted(c.internalId)) {
            ++skipped;
            continue;
        }
        m_queue.enqueue({c.internalId, maxSize});
    }
    if (skipped > 0) {
        qCDebug(lcSnapAssistCapture) << "captureCandidates: skipped" << skipped << "recently-posted of"
                                     << candidates.size() << "; queued=" << m_queue.size();
    }
    if (m_queue.isEmpty() || m_busy) {
        return;
    }
    QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
}

bool SnapAssistThumbnailCapture::wasRecentlyPosted(const QUuid& handle) const
{
    return m_recentlyPostedSet.contains(handle);
}

void SnapAssistThumbnailCapture::markRecentlyPosted(const QUuid& handle)
{
    if (m_recentlyPostedSet.contains(handle)) {
        // Already tracked — leave the queue position alone. Re-touching here
        // would require an O(n) lookup-and-erase to refresh recency, and the
        // daemon's QCache already handles per-handle recency on its own
        // insert path. Keeping insertion order stable means the eviction
        // bound mirrors first-post time, which is what we need to match the
        // daemon's "saw N distinct handles" budget.
        return;
    }
    m_recentlyPostedSet.insert(handle);
    m_recentlyPostedOrder.enqueue(handle);
    while (m_recentlyPostedOrder.size() > kRecentPostedCapacity) {
        const QUuid evicted = m_recentlyPostedOrder.dequeue();
        m_recentlyPostedSet.remove(evicted);
    }
}

void SnapAssistThumbnailCapture::ensureScene()
{
    if (m_scene) {
        return;
    }
    // ExportMode::Image: bufferAsImage() returns a usable QImage after
    // each render. setVisible(false) keeps the FBO alive without ever
    // mapping the scene to an output. Disable automatic repaint — we
    // drive update() explicitly once per capture so we know exactly
    // which frame we're reading back.
    m_scene = std::make_unique<KWin::OffscreenQuickScene>(KWin::OffscreenQuickView::ExportMode::Image);
    m_scene->setVisible(false);
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
    m_busy = true;
    Pending p = m_queue.dequeue();
    ensureScene();
    if (!m_scene || !m_scene->rootItem()) {
        // Scene didn't load — abort the rest of the queue rather than spin
        // forever on a broken QML resource. Snap-assist falls back to icons.
        m_queue.clear();
        m_busy = false;
        return;
    }

    QQuickItem* root = m_scene->rootItem();
    root->setProperty("wId", QVariant::fromValue(p.internalId));
    root->setProperty("boxSize", QVariant::fromValue(QSizeF(p.maxSize)));
    m_scene->setGeometry(QRect(QPoint(0, 0), p.maxSize));

    attemptCapture(p, kRenderSettleMs, /*retriesLeft=*/1);
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
            m_queue.clear();
            m_busy = false;
            QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
            return;
        }
        m_scene->update();
        QImage image = m_scene->bufferAsImage();
        if (image.isNull() && retriesLeft > 0) {
            // Compositor occasionally drops the first frame after wId is
            // bound. Wait one more frame interval — same Pending, no queue
            // shuffle — before giving up.
            attemptCapture(p, kRenderRetryMs, retriesLeft - 1);
            return;
        }
        if (!image.isNull()) {
            // bufferAsImage commonly returns Format_ARGB32_Premultiplied
            // (matches the FBO layout). Convert to plain ARGB32 so PNG
            // round-trips correctly — premultiplied alpha through PNG
            // encode/decode produces visibly darkened semi-transparent
            // edges otherwise.
            if (image.format() != QImage::Format_ARGB32) {
                image = image.convertToFormat(QImage::Format_ARGB32);
            }
            postThumbnail(p.internalId, image);
            markRecentlyPosted(p.internalId);
        } else {
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.internalId.toString()
                                         << "produced empty image after retry";
        }
        QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
    });
}

void SnapAssistThumbnailCapture::postThumbnail(const QUuid& internalId, const QImage& image)
{
    QByteArray bytes;
    {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            qCWarning(lcSnapAssistCapture) << "PNG encode failed for" << internalId.toString();
            return;
        }
    }
    const QString compositorHandle = internalId.toString();
    const QString dataUrl = QStringLiteral("data:image/png;base64,") + QString::fromUtf8(bytes.toBase64());

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setSnapAssistThumbnail"));
    msg << compositorHandle << dataUrl;

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [compositorHandle](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCDebug(lcSnapAssistCapture) << "setSnapAssistThumbnail D-Bus call failed for" << compositorHandle << ":"
                                         << w->error().message();
        }
    });
}

} // namespace PlasmaZones

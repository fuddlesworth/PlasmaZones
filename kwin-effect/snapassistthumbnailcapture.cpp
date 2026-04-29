// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailcapture.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/offscreenquickview.h>

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusMessage>
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
    m_queue.clear();
    for (const auto& c : candidates) {
        if (c.compositorHandle.isEmpty() || c.internalId.isNull()) {
            continue;
        }
        m_queue.enqueue({c.compositorHandle, c.internalId, maxSize});
    }
    if (m_queue.isEmpty() || m_busy) {
        return;
    }
    QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
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

    QTimer::singleShot(kRenderSettleMs, this, [this, p]() {
        if (!m_scene) {
            m_busy = false;
            return;
        }
        m_scene->update();
        QImage image = m_scene->bufferAsImage();
        if (!image.isNull()) {
            // bufferAsImage commonly returns Format_ARGB32_Premultiplied
            // (matches the FBO layout). Convert to plain ARGB32 so PNG
            // round-trips correctly — premultiplied alpha through PNG
            // encode/decode produces visibly darkened semi-transparent
            // edges otherwise.
            if (image.format() != QImage::Format_ARGB32) {
                image = image.convertToFormat(QImage::Format_ARGB32);
            }
            postThumbnail(p.compositorHandle, image);
        } else {
            qCDebug(lcSnapAssistCapture) << "captureCandidates:" << p.compositorHandle << "produced empty image";
        }
        QTimer::singleShot(0, this, &SnapAssistThumbnailCapture::processNext);
    });
}

void SnapAssistThumbnailCapture::postThumbnail(const QString& compositorHandle, const QImage& image)
{
    QByteArray bytes;
    {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            qCWarning(lcSnapAssistCapture) << "PNG encode failed for" << compositorHandle;
            return;
        }
    }
    const QString dataUrl = QStringLiteral("data:image/png;base64,") + QString::fromUtf8(bytes.toBase64());

    QDBusMessage msg = QDBusMessage::createMethodCall(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Overlay, QStringLiteral("setSnapAssistThumbnail"));
    msg << compositorHandle << dataUrl;
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace PlasmaZones

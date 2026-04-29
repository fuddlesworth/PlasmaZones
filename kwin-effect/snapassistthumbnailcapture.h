// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QQueue>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector>

#include <memory>

class QImage;

namespace KWin {
class OffscreenQuickScene;
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
 * as a base64 PNG data URL — the daemon decodes once into its bounded LRU
 * QImage cache and never carries the encoded form past that point.
 */
class SnapAssistThumbnailCapture : public QObject
{
    Q_OBJECT

public:
    /// (compositorHandle, internalId) pair. compositorHandle is the QUuid in
    /// braced toString() form — that's the key the daemon's image provider
    /// uses; internalId is the same QUuid in raw form, which is what
    /// WindowThumbnail's @c wId property accepts.
    struct Candidate
    {
        QString compositorHandle;
        QUuid internalId;
    };

    explicit SnapAssistThumbnailCapture(QObject* parent = nullptr);
    ~SnapAssistThumbnailCapture() override;

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
    void captureCandidates(const QVector<Candidate>& candidates, QSize maxSize = QSize(256, 256));

private Q_SLOTS:
    void processNext();

private:
    void ensureScene();
    void postThumbnail(const QString& compositorHandle, const QImage& image);

    struct Pending
    {
        QString compositorHandle;
        QUuid internalId;
        QSize maxSize;
    };

    std::unique_ptr<KWin::OffscreenQuickScene> m_scene;
    QQueue<Pending> m_queue;
    bool m_busy = false;
};

} // namespace PlasmaZones

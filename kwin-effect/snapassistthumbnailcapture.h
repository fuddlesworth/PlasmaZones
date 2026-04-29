// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSize>
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
    void postThumbnail(const QUuid& internalId, const QImage& image);

    struct Pending
    {
        QUuid internalId;
        QSize maxSize;
    };

    /// Read back the current scene buffer for @p p; on a null buffer, retry
    /// once with a longer delay before giving up. Compositor stalls
    /// occasionally drop the very first frame after binding @c wId to a
    /// fresh window; one retry is enough in practice and falls back to the
    /// icon path otherwise.
    void attemptCapture(Pending p, int delayMs, int retriesLeft);

    /// Mark @p handle as posted to the daemon, evicting the oldest entry
    /// if the bookkeeping set is at capacity. Called only after a thumbnail
    /// successfully posts; failures leave the handle un-tracked so the next
    /// snap-assist invocation will retry.
    void markRecentlyPosted(const QUuid& handle);

    /// True when @p handle was recently posted to the daemon and is
    /// (probably) still resident in the daemon's bounded LRU cache.
    /// Mirrors the daemon's @c SnapAssistThumbnailProvider::kCacheCapacity
    /// so this side stays in sync with the daemon's eviction window.
    bool wasRecentlyPosted(const QUuid& handle) const;

    /// Mirror of the daemon-side LRU cap. Kept here as a literal — the
    /// effect deliberately doesn't depend on the daemon's header. If the
    /// daemon's capacity ever grows past this, the worst case is a spurious
    /// re-capture; if the daemon's shrinks below this, we'll skip captures
    /// the daemon has already evicted (cache miss → QML icon fallback). In
    /// either direction the failure is bounded and self-correcting.
    static constexpr int kRecentPostedCapacity = 24;

    std::unique_ptr<KWin::OffscreenQuickScene> m_scene;
    QQueue<Pending> m_queue;
    /// Bookkeeping for @ref wasRecentlyPosted: O(1) membership via the set,
    /// O(1) oldest-first eviction via the queue. Kept strictly in sync.
    QSet<QUuid> m_recentlyPostedSet;
    QQueue<QUuid> m_recentlyPostedOrder;
    bool m_busy = false;
};

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/types/dmabufthumbnail.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QCache>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSet>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Texture-type QML image provider that imports DMA-BUF window thumbnails
 *        as GPU textures — the zero-copy counterpart to the QImage-based
 *        SnapAssistThumbnailProvider.
 *
 * General window-thumbnail transport: not tied to any one feature. Snap-assist
 * is the current (only) consumer, but the import path is reusable by future
 * preview features (e.g. a window-scroll/overview strip).
 *
 * A producer (the kwin-effect today) exports each rendered thumbnail as a
 * dma-buf and hands the fd to the daemon via @c setWindowThumbnailDmabuf.
 * @ref insert stores the descriptor (taking ownership of a @c dup of the fd)
 * keyed by compositor handle and returns a QML URL under
 * @c image://plasmazones-dmabuf-thumbnail/. When QML loads that URL,
 * @ref requestTexture hands back a QQuickTextureFactory whose @c createTexture
 * (on the render thread) imports the dma-buf into a GPU texture, copies it into
 * an owned texture, and exposes it as a QSGTexture — no read-back, no re-upload.
 *
 * RHI backends: Vulkan (the daemon's default, via VK_EXT_external_memory_dma_buf)
 * and OpenGL (via EGL_EXT_image_dma_buf_import; see dmabufglimport). The two
 * backends differ on ONE class of descriptor: a modifier-less buffer with a
 * padded stride or non-zero offset imports fine on GL (the EGL attribute
 * list carries stride/offset explicitly) but is REJECTED by the Vulkan
 * importer, whose implicit-LINEAR layout cannot express either. Any other
 * backend — and any import failure — yields no texture. NOTE this failure is
 * NOT communicated back to the producer: the D-Bus reply (accepted=true) was
 * sent when the descriptor was STORED, long before the render-thread import
 * runs, so the producer will not re-capture. The QML consumer must therefore
 * degrade a failed load to its icon fallback (the snap-assist card gates on
 * Image.status for exactly this).
 *
 * Thread-safety: @ref insert / @ref markRevealed / @ref clear run on the GUI
 * thread (D-Bus slot); @ref requestTexture runs on Qt's image-loader thread;
 * @ref urlFor is read from the GUI thread. All access to the pending store
 * goes through @ref m_mutex. The actual GPU import happens later, on the
 * scene-graph render thread, inside the returned factory.
 */
class DmabufTextureProvider : public QQuickImageProvider
{
public:
    /// QML provider id — the host segment of @c image://<id>/...
    static constexpr const char* ProviderId = "plasmazones-dmabuf-thumbnail";

    DmabufTextureProvider();
    ~DmabufTextureProvider() override;

    /**
     * @brief Store the dma-buf descriptor for @p compositorHandle and return its
     *        QML URL.
     *
     * The URL embeds a monotonic generation counter purely as a cache-buster so
     * QML's QQuickPixmap cache re-fetches when a fresh buffer arrives; it is NOT
     * a version selector. The store is keyed by handle, so @ref requestTexture
     * always serves the latest buffer for a handle regardless of the generation
     * in the requested URL (latest-wins — matching SnapAssistThumbnailProvider).
     *
     * Takes ownership of a @c dup of @c desc.fd; any previous descriptor for the
     * same handle is closed and replaced. Returns an empty string when the fd is
     * invalid, the handle is empty/unusable as a key, or the store refuses the
     * entry.
     */
    QString insert(const QString& compositorHandle, const DmabufThumbnailDesc& desc);

    /**
     * @brief Mark @p compositorHandle's stored descriptor as revealed.
     *
     * Called by the consumer once the producer's render-completion fence has
     * signaled (or on the fence-less defensive paths, immediately). Until
     * then @ref urlFor and @ref requestTexture withhold the entry, so a
     * warm-cache re-attach can never hand QML a buffer whose render may
     * still be in flight.
     *
     * @p url must be the URL @ref insert returned for the generation whose
     * fence signaled: the store is latest-wins per handle, and a reveal is
     * applied only when the stored entry still IS that generation —
     * otherwise an older generation's late fence would reveal a newer,
     * still-rendering buffer.
     */
    void markRevealed(const QString& compositorHandle, const QString& url);

    /**
     * @brief Look up the current QML URL for @p compositorHandle without
     *        inserting. Returns an empty string when the handle has no stored
     *        descriptor, or its reveal fence has not signaled yet. Used by
     *        showSnapAssist's warm-cache pass so a dma-buf thumbnail survives
     *        a re-show exactly like a raw-pixel one — without this, the
     *        producer's recently-posted dedup skip plus the missing warm URL
     *        stranded every dma-buf candidate on its icon from the second
     *        show onward.
     */
    QString urlFor(const QString& compositorHandle) const;

    QQuickTextureFactory* requestTexture(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// Drop every stored descriptor, closing their dup'd fds. Mirrors
    /// SnapAssistThumbnailProvider::clear so the idle-grace trim reclaims both
    /// providers together.
    void clear();

private:
    /// Store entry: the owned descriptor plus its last-issued URL. Owns the
    /// dup'd fd (closed in the destructor, which QCache invokes on
    /// replacement, LRU eviction and clear()). The reveal latch lives in the
    /// separate @ref m_revealed set, NOT here: QCache::object() promotes the
    /// entry to MRU as a side effect, and gating the reveal test on an
    /// object() lookup let a permanently-unrevealed entry (fence timeout) be
    /// re-promoted by every warm-cache miss — pinning its fd and its LRU
    /// slot until the idle-grace clear.
    struct PendingEntry
    {
        DmabufThumbnailDesc desc;
        QString url;
        PendingEntry() = default;
        PendingEntry(const PendingEntry&) = delete;
        PendingEntry& operator=(const PendingEntry&) = delete;
        ~PendingEntry();
    };

    mutable QMutex m_mutex;
    /// Keyed by normalised (unbraced) handle. Bounded by the same shared
    /// entry budget as the pixel provider's cache: each entry pins a dup'd
    /// dma-buf fd AND (transitively) the producer's GPU buffer, so an
    /// unbounded map would accumulate one of each per distinct handle
    /// between idle-grace trims — in an active session the trim timer is
    /// restarted on every re-show, so "between trims" can be the whole
    /// session. LRU eviction (QCache promotes on requestTexture/urlFor
    /// access) reclaims the oldest entries; the idle-grace clear() remains
    /// the end-of-use reclaim for the remainder.
    QCache<QString, PendingEntry> m_pending{PhosphorProtocol::Service::SnapAssistThumbnailCacheCapacity};
    /// Handles whose render-completion fence has signaled (or whose reveal
    /// was forced by a fence-less defensive path). Checked WITHOUT touching
    /// the cache so an un-revealed handle never promotes its entry; pruned
    /// on re-insert (new generation = un-revealed again) and on clear().
    /// May transiently name evicted entries; urlFor/requestTexture also
    /// require the entry itself, so a stale set member is inert.
    QSet<QString> m_revealed;
    quint32 m_generation = 0;
};

} // namespace PlasmaZones

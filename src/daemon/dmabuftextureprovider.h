// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../core/dmabufthumbnail.h"

#include <QHash>
#include <QMutex>
#include <QQuickImageProvider>
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
 * and OpenGL (via EGL_EXT_image_dma_buf_import; see dmabufglimport). Any other
 * backend returns no texture and the consumer falls back to its raw-pixel path
 * (the effect's accepted=false contract re-captures).
 *
 * Thread-safety: @ref insert / @ref clear run on the GUI thread (D-Bus slot);
 * @ref requestTexture runs on Qt's image-loader thread. All access to the
 * pending map goes through @ref m_mutex. The actual GPU import happens later,
 * on the scene-graph render thread, inside the returned factory.
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
     * invalid or the handle is empty.
     */
    QString insert(const QString& compositorHandle, const DmabufThumbnailDesc& desc);

    QQuickTextureFactory* requestTexture(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// Drop every stored descriptor, closing their dup'd fds. Mirrors
    /// SnapAssistThumbnailProvider::clear so the idle-grace trim reclaims both
    /// providers together.
    void clear();

private:
    static QString normaliseHandle(const QString& handle);
    static QString makeUrl(const QString& handle, quint32 generation);

    mutable QMutex m_mutex;
    /// Keyed by normalised (unbraced) handle. Each entry owns a dup'd fd, closed
    /// on replacement (re-insert for the same handle) or clear(). Entries are
    /// NOT individually evicted; the map is bounded by the consumer's
    /// idle-grace trim, which clear()s it after snap-assist stays dismissed
    /// (see OverlayService's trim timer). In practice the live count tracks the
    /// snap-assist candidate set (a handful), not session history.
    QHash<QString, DmabufThumbnailDesc> m_pending;
    quint32 m_generation = 0;
};

} // namespace PlasmaZones

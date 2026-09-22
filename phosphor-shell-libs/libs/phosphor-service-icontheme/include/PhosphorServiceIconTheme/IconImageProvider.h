// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceIconTheme/phosphorserviceicontheme_export.h>

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace PhosphorServiceIconTheme {

/// QML image provider mounted at `image://phosphor-service-icontheme/`.
/// Callers that can't expose `QImage` directly as `Image.source`
/// because `source` is `QUrl` (the canonical example is the SNI item
/// model exposing tray IconPixmap blobs) hand the provider the image
/// via `setImage()`, then expose a URL of the form
/// `image://phosphor-service-icontheme/<id>?v=<cacheKey>` to QML; the
/// engine routes the URL back to `requestImage()`, which returns the
/// latest QImage from a thread-safe registry keyed by id.
///
/// The registry is process-global (a static member) rather than
/// per-engine because shells may construct multiple engines during
/// reload cycles, and the icons are valid across them. Locking is
/// coarse (one mutex for the whole map), icon updates are at human
/// timescales, so contention is a non-issue.
class PHOSPHORSERVICEICONTHEME_EXPORT IconImageProvider : public QQuickImageProvider
{
    Q_DISABLE_COPY_MOVE(IconImageProvider)
public:
    IconImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// Register or replace the image associated with `id`. Called
    /// from publishers (e.g. an SNI item model) when an item's
    /// `iconImage()` changes, so the next frame's `Image.source`
    /// rebind reads the fresh data. Passing a null QImage is treated
    /// as a clear of the entry, equivalent to `clearImage(id)`;
    /// publishers that want to publish "no icon" should drop the URL
    /// instead (Qt's `Image.source = ""` is the cleaner signal).
    static void setImage(const QString& id, const QImage& image);

    /// Drop the image. Called on item-unregistered to keep the
    /// registry from growing across crashes / disconnects of long-
    /// lived sessions.
    static void clearImage(const QString& id);

private:
    static QMutex s_mutex;
    static QHash<QString, QImage> s_registry;
};

} // namespace PhosphorServiceIconTheme

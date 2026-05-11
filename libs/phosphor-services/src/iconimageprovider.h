// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

namespace PhosphorServices {

/// QML image provider mounted at `image://phosphor-services/`. The
/// SNI item model can't expose `QImage` directly as `Image.source`
/// because `source` is `QUrl`; the provider is the canonical Qt
/// indirection — model exposes a URL string, the engine routes the
/// URL back to `requestImage()`, which hands out the latest QImage
/// from a thread-safe registry keyed by item id.
///
/// The registry is process-global (a static member) rather than
/// per-engine because shells may construct multiple engines during
/// reload cycles, and the icons are valid across them. Locking is
/// coarse (one mutex for the whole map) — tray-icon updates are
/// per-second at worst, so contention is a non-issue.
class IconImageProvider : public QQuickImageProvider
{
public:
    IconImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    /// Register / replace the image associated with `id`. Called from
    /// the model when an item's iconImage() changes, so the next
    /// frame's `Image.source` rebind reads the fresh data.
    static void setImage(const QString& id, const QImage& image);

    /// Drop the image. Called on item-unregistered to keep the
    /// registry from growing across crashes / disconnects of long-
    /// lived sessions.
    static void clearImage(const QString& id);

private:
    static QMutex s_mutex;
    static QHash<QString, QImage> s_registry;
};

} // namespace PhosphorServices

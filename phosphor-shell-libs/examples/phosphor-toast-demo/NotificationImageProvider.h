// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace PhosphorToastDemo {

// Serves decoded notification images to QML via `image://notification/<id>`.
// The freedesktop `image-data` hint decodes to a QImage with no file path,
// which a url-based Image element can't load directly, so the demo caches
// each notification's image by id and hands it back here. requestImage runs
// on a Qt image-loader thread, so the cache is mutex-guarded; cacheImage /
// dropImage are called from the GUI thread as notifications arrive and close.
class NotificationImageProvider : public QQuickImageProvider
{
public:
    NotificationImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    Q_DISABLE_COPY_MOVE(NotificationImageProvider)

    ~NotificationImageProvider() override = default;

    // GUI thread: remember a notification's decoded image.
    void cacheImage(uint id, const QImage& image)
    {
        const QMutexLocker locker(&m_mutex);
        m_images.insert(id, image);
    }

    // GUI thread: forget a closed notification's image so the cache stays
    // bounded to live notifications.
    void dropImage(uint id)
    {
        const QMutexLocker locker(&m_mutex);
        m_images.remove(id);
    }

    // Image-loader thread: id is the notification id from the url. Returns a
    // null QImage (blank) for an unknown id, which Image renders as nothing.
    [[nodiscard]] QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        QImage image;
        {
            const QMutexLocker locker(&m_mutex);
            image = m_images.value(id.toUInt());
        }
        if (image.isNull()) {
            return image;
        }
        if (size != nullptr) {
            *size = image.size();
        }
        // isEmpty() (not isValid()) so a 0x0 request falls through to the
        // full image rather than scaling to a null result.
        if (!requestedSize.isEmpty() && requestedSize != image.size()) {
            return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return image;
    }

private:
    QMutex m_mutex;
    QHash<uint, QImage> m_images;
};

} // namespace PhosphorToastDemo

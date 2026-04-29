// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailprovider.h"

namespace PlasmaZones {

SnapAssistThumbnailProvider::SnapAssistThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_cache(kCacheCapacity)
{
}

QImage SnapAssistThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QString handle = id.section(QLatin1Char('/'), 0, 0);

    QImage out;
    {
        QMutexLocker lock(&m_mutex);
        QImage* cached = m_cache.object(handle);
        if (!cached || cached->isNull()) {
            if (size) {
                *size = QSize(0, 0);
            }
            return QImage();
        }
        // QImage is implicitly shared — assignment bumps a refcount, not a
        // pixel copy. Lift the value out before unlocking so the cache can
        // evict the entry without invalidating the returned image.
        out = *cached;
    }

    if (size) {
        *size = out.size();
    }
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && (out.width() > requestedSize.width() || out.height() > requestedSize.height())) {
        return out.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return out;
}

QString SnapAssistThumbnailProvider::insert(const QString& compositorHandle, QImage image)
{
    if (compositorHandle.isEmpty() || image.isNull()) {
        return QString();
    }
    quint32 gen = 0;
    {
        QMutexLocker lock(&m_mutex);
        gen = ++m_generations[compositorHandle];
        m_cache.insert(compositorHandle, new QImage(std::move(image)));
    }
    return makeUrl(compositorHandle, gen);
}

QString SnapAssistThumbnailProvider::urlFor(const QString& compositorHandle) const
{
    if (compositorHandle.isEmpty()) {
        return QString();
    }
    QMutexLocker lock(&m_mutex);
    if (!m_cache.contains(compositorHandle)) {
        return QString();
    }
    return makeUrl(compositorHandle, m_generations.value(compositorHandle));
}

QString SnapAssistThumbnailProvider::makeUrl(const QString& handle, quint32 generation)
{
    return QStringLiteral("image://%1/%2/%3").arg(QString::fromLatin1(kProviderId), handle).arg(generation);
}

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailprovider.h"

namespace PlasmaZones {

SnapAssistThumbnailProvider::SnapAssistThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_cache(CacheCapacity)
{
}

QImage SnapAssistThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // QML strips the `image://<provider>/` prefix and hands us the path
    // segment. Cache keys are normalised to the unbraced UUID form on
    // insert; we apply the same normalisation here so a caller that
    // hand-builds an `image://plasmazones-snapassist/{uuid}/N` URL still
    // resolves to the same cache slot.
    const QString handle = normaliseHandle(id.section(QLatin1Char('/'), 0, 0));

    QImage out;
    {
        QMutexLocker lock(&m_mutex);
        Entry* cached = m_cache.object(handle);
        if (!cached || cached->image.isNull()) {
            if (size) {
                *size = QSize(0, 0);
            }
            return QImage();
        }
        // QImage is implicitly shared — assignment bumps a refcount, not a
        // pixel copy. Lift the value out before unlocking so the cache can
        // evict the entry without invalidating the returned image.
        out = cached->image;
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
    const QString key = normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    const quint32 gen = ++m_generation;
    const QString url = makeUrl(key, gen);
    auto* entry = new Entry{std::move(image), url};
    m_cache.insert(key, entry);
    return url;
}

QString SnapAssistThumbnailProvider::urlFor(const QString& compositorHandle) const
{
    if (compositorHandle.isEmpty()) {
        return QString();
    }
    const QString key = normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    Entry* cached = m_cache.object(key);
    return cached ? cached->url : QString();
}

void SnapAssistThumbnailProvider::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
}

QString SnapAssistThumbnailProvider::makeUrl(const QString& handle, quint32 generation)
{
    return QStringLiteral("image://%1/%2/%3").arg(QString::fromLatin1(ProviderId)).arg(handle).arg(generation);
}

QString SnapAssistThumbnailProvider::normaliseHandle(const QString& handle)
{
    if (handle.size() >= 2 && handle.startsWith(QLatin1Char('{')) && handle.endsWith(QLatin1Char('}'))) {
        return handle.mid(1, handle.size() - 2);
    }
    return handle;
}

} // namespace PlasmaZones

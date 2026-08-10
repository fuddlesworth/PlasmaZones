// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapassistthumbnailprovider.h"
#include "thumbnailurlutil.h"

namespace PlasmaZones {

SnapAssistThumbnailProvider::SnapAssistThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_cache(CacheMaxBytes)
{
}

QImage SnapAssistThumbnailProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // QML strips the `image://<provider>/` prefix and hands us the path
    // segment. Cache keys are normalised to the unbraced UUID form on
    // insert; we apply the same normalisation here so a caller that
    // hand-builds an `image://plasmazones-snapassist/{uuid}/N` URL still
    // resolves to the same cache slot.
    const QString handle = ThumbnailUrl::normaliseHandle(id.section(QLatin1Char('/'), 0, 0));

    QImage out;
    {
        QMutexLocker lock(&m_mutex);
        Entry* cached = m_cache.object(handle);
        if (!cached || cached->image.isNull()) {
            if (size) {
                *size = QSize(0, 0);
            }
            // A null return makes the QML Image land in Image.Error, which
            // the snap-assist card treats as "fall back to the icon" — the
            // designed outcome for an evicted/trimmed entry.
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
    const QString key = ThumbnailUrl::normaliseHandle(compositorHandle);
    // Re-check AFTER normalisation: "{}" normalises to an empty key, and a
    // '/'-bearing handle would be stored under a key requestImage can never
    // reconstruct (it splits at the first '/'). Refusing here returns an
    // empty URL → the producer sees accepted=false and does NOT mark the
    // handle recently-posted, so nothing latches on an unservable entry.
    if (!ThumbnailUrl::isValidHandleKey(key)) {
        return QString();
    }
    // Byte-denominated cost keeps the documented memory bound honest for any
    // image size the boundary admits. Reject an image larger than the whole
    // budget outright — unreachable via the D-Bus/service boundaries (the
    // 1024² ceiling is 4 MiB < CacheMaxBytes) but a direct C++ caller could
    // otherwise hand QCache a cost that trims the ENTIRE cache before the
    // insert is refused anyway. Checked before the generation bump so a
    // rejected insert doesn't burn a cache-buster value.
    if (image.sizeInBytes() > CacheMaxBytes) {
        return QString();
    }
    QMutexLocker lock(&m_mutex);
    const quint32 gen = ++m_generation;
    const QString url = ThumbnailUrl::makeUrl(ProviderId, key, gen);
    const int cost = static_cast<int>(image.sizeInBytes());
    auto* entry = new Entry{std::move(image), url};
    // Defensive: with the size reject above, cost <= maxCost always holds
    // and QCache::insert cannot refuse — but a refused insert deletes the
    // entry immediately, and returning its URL would hand the producer an
    // accepted=true that latches an unservable handle in its dedup window.
    if (!m_cache.insert(key, entry, cost)) {
        return QString();
    }
    return url;
}

QString SnapAssistThumbnailProvider::urlFor(const QString& compositorHandle) const
{
    if (compositorHandle.isEmpty()) {
        return QString();
    }
    const QString key = ThumbnailUrl::normaliseHandle(compositorHandle);
    QMutexLocker lock(&m_mutex);
    Entry* cached = m_cache.object(key);
    return cached ? cached->url : QString();
}

void SnapAssistThumbnailProvider::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
}

} // namespace PlasmaZones

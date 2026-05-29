// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/IconImageProvider.h>

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QUrl>

Q_LOGGING_CATEGORY(lcImageProvider, "phosphor.service.icontheme.imageprovider")

namespace PhosphorServiceIconTheme {

QMutex IconImageProvider::s_mutex;
QHash<QString, QImage> IconImageProvider::s_registry;

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // Two transformations from URL → registry key:
    //   1. Strip the ?v=<cacheKey> query string — that suffix exists
    //      only to force QML's Image element to re-fetch when the
    //      underlying QImage data changes (Image only reloads when
    //      the URL string differs). The PUBLISH side never sees the
    //      cacheKey, so we mustn't either when looking up.
    //   2. Percent-decode `%7C` back to `|` etc. — Qt's image-provider
    //      dispatch hands us the URL-path form, which on this version
    //      preserves percent-encoding. The publish side uses the
    //      decoded form (clean printable key).
    // Both transformations are no-ops when the input already lacks
    // them, so the cleanup is safe to run unconditionally.
    QString lookupId = id;
    const int q = lookupId.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        lookupId.truncate(q);
    }
    lookupId = QUrl::fromPercentEncoding(lookupId.toUtf8());

    // Copy the QImage out under the lock, then release before any
    // expensive transformation. Earlier rev held the mutex across
    // `img.scaled(SmoothTransformation)` which could block publishers
    // (setImage / clearImage) for several milliseconds per request.
    // QImage is implicitly shared (copy-on-write), so the copy is
    // O(1) and doesn't actually duplicate pixel data.
    QImage img;
    {
        QMutexLocker lock(&s_mutex);
        qCDebug(lcImageProvider) << "requestImage id=" << id << " lookup=" << lookupId
                                 << " requestedSize=" << requestedSize << " registry size=" << s_registry.size();
        auto it = s_registry.constFind(lookupId);
        if (it == s_registry.constEnd()) {
            // Surface the miss to logs so a future regression (wrong key
            // format, missed publish callsite) is debuggable without a
            // gdb session.
            qCWarning(lcImageProvider) << "no registered image for id" << id << "(stripped+decoded:" << lookupId << ")";
            if (size) {
                *size = QSize(0, 0);
            }
            return {};
        }
        img = *it;
    }
    if (size) {
        *size = img.size();
    }
    // requestedSize is honoured only when the caller asked for it
    // (i.e., sourceSize is set on the Image). The resolver already
    // produced the right size from `preferredIconSize`; rescaling
    // here on top of that would compound interpolation artefacts.
    if (requestedSize.isValid() && requestedSize != img.size()) {
        return img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img;
}

void IconImageProvider::setImage(const QString& id, const QImage& image)
{
    QMutexLocker lock(&s_mutex);
    if (image.isNull()) {
        s_registry.remove(id);
    } else {
        s_registry.insert(id, image);
    }
}

void IconImageProvider::clearImage(const QString& id)
{
    QMutexLocker lock(&s_mutex);
    s_registry.remove(id);
}

} // namespace PhosphorServiceIconTheme

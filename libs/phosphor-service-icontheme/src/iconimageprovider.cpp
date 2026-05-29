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
    //   1. Strip the ?v=<cacheKey> query string, that suffix exists
    //      only to force QML's Image element to re-fetch when the
    //      underlying QImage data changes (Image only reloads when
    //      the URL string differs). The PUBLISH side never sees the
    //      cacheKey, so we mustn't either when looking up.
    //   2. Percent-decode `%7C` back to `|` etc., Qt's image-provider
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
    // expensive transformation OR logging. Earlier rev held the mutex
    // across `img.scaled(SmoothTransformation)` which could block
    // publishers (setImage / clearImage) for several milliseconds per
    // request. QImage is implicitly shared (copy-on-write), so the
    // copy is O(1) and doesn't actually duplicate pixel data. Logging
    // ran under the lock too; a slow custom Qt log handler (network
    // forwarder, file logger) would have serialised every publisher
    // behind the consumer's log sink. Capture both the lookup result
    // and the registry size, release the lock, then log.
    QImage img;
    bool hit = false;
    qsizetype registrySize = 0;
    {
        QMutexLocker lock(&s_mutex);
        registrySize = s_registry.size();
        auto it = s_registry.constFind(lookupId);
        if (it != s_registry.constEnd()) {
            img = *it;
            hit = true;
        }
    }
    if (!hit) {
        // Surface the miss to logs so a future regression (wrong key
        // format, missed publish callsite) is debuggable without a
        // gdb session. We log only on the miss path: hits run every
        // frame for every visible Image and during shell startup with
        // many tray icons even a qCDebug stream is enough to overwhelm
        // a slow log handler. Capture registry size in the miss line
        // for context.
        qCWarning(lcImageProvider) << "no registered image for id" << id << "(stripped+decoded:" << lookupId
                                   << ", registry size=" << registrySize << ", requestedSize=" << requestedSize << ")";
        if (size) {
            *size = QSize(0, 0);
        }
        return {};
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
    // requestImage truncates the lookup id at the first '?' to strip
    // the cache-bust query string. A published id containing '?' would
    // therefore be unreachable. Reject at publish time so the failure
    // is logged at the publishing call site rather than as a silent
    // miss on the consumer side.
    if (id.contains(QLatin1Char('?'))) {
        qCWarning(lcImageProvider) << "rejected setImage id containing '?'; ids must not collide with the cache-bust "
                                      "query string. id="
                                   << id;
        return;
    }
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "iconimageprovider.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <QUrl>

Q_LOGGING_CATEGORY(lcImageProvider, "phosphorservices.imageprovider")

namespace PhosphorServices {

QMutex IconImageProvider::s_mutex;
QHash<QString, QImage> IconImageProvider::s_registry;

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QMutexLocker lock(&s_mutex);
    // Qt's behaviour around id encoding is inconsistent across
    // versions and even across what part of the URL the request
    // originated from. Some versions pass the percent-encoded form
    // (":1.67%7C/StatusNotifierItem"), others pass the decoded form
    // (":1.67|/StatusNotifierItem"). We publish under the decoded
    // form (clean printable key) and try both on lookup so we don't
    // have to guess what Qt version is in play. The percent-decode
    // is a no-op when the id was already decoded, so this is free
    // on the happy path.
    auto it = s_registry.constFind(id);
    if (it == s_registry.constEnd()) {
        const QString decoded = QUrl::fromPercentEncoding(id.toUtf8());
        it = s_registry.constFind(decoded);
    }
    if (it == s_registry.constEnd()) {
        // Surface the miss to logs so a future regression (wrong key
        // format, missed publish callsite) is debuggable without a
        // gdb session. Throttled implicitly by Qt's "same message
        // collapsed" output policy when QT_LOGGING_RULES is default.
        qCWarning(lcImageProvider) << "no registered image for id" << id
                                   << "(decoded:" << QUrl::fromPercentEncoding(id.toUtf8()) << ")";
        if (size) {
            *size = QSize(0, 0);
        }
        return {};
    }
    const QImage& img = *it;
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

} // namespace PhosphorServices

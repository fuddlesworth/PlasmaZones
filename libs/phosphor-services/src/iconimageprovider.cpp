// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "iconimageprovider.h"

#include <QMutexLocker>

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
    auto it = s_registry.constFind(id);
    if (it == s_registry.constEnd()) {
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

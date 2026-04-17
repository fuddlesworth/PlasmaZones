// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIdentity/WindowId.h>

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPixmap>

namespace PhosphorIdentity {
namespace WindowId {

QString iconToDataUrl(const QIcon& icon, int size)
{
    if (icon.isNull()) {
        return QString();
    }
    QPixmap pix = icon.pixmap(size, size);
    if (pix.isNull()) {
        return QString();
    }
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    if (!pix.toImage().save(&buffer, "PNG")) {
        return QString();
    }
    return QStringLiteral("data:image/png;base64,") + QString::fromUtf8(ba.toBase64());
}

} // namespace WindowId
} // namespace PhosphorIdentity

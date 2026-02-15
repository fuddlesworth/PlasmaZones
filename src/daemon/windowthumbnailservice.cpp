// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowthumbnailservice.h"
#include "../core/logging.h"

#include <QBuffer>
#include <QDataStream>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QFileDevice>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QtConcurrent>

#include <fcntl.h>
#include <unistd.h>

namespace PlasmaZones {

static constexpr int THUMBNAIL_MAX_SIZE = 256;

static const QString kScreenShot2Service = QStringLiteral("org.kde.KWin.ScreenShot2");
static const QString kScreenShot2Path    = QStringLiteral("/org/kde/KWin/ScreenShot2");
static const QString kScreenShot2Iface   = QStringLiteral("org.kde.KWin.ScreenShot2");

// KWin ScreenShot2 protocol: metadata in D-Bus reply; raw QImage bytes in pipe.
// See: KWin screenshotdbusinterface2.cpp, Spectacle ImagePlatformKWin.cpp

WindowThumbnailService::WindowThumbnailService(QObject* parent)
    : QObject(parent)
{
}

bool WindowThumbnailService::isAvailable() const
{
    // Lightweight name-owner check — no synchronous introspection
    auto* iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(kScreenShot2Service);
}

static QImage readImageFromPipe(int readFd, const QVariantMap& metadata)
{
    if (metadata.value(QLatin1String("type")).toString() != QLatin1String("raw")) {
        return QImage();
    }

    bool ok = false;
    const int width = metadata.value(QLatin1String("width")).toInt(&ok);
    if (!ok || width <= 0 || width > 10000) {
        return QImage();
    }
    const int height = metadata.value(QLatin1String("height")).toInt(&ok);
    if (!ok || height <= 0 || height > 10000) {
        return QImage();
    }
    const uint format = metadata.value(QLatin1String("format")).toUInt(&ok);
    if (!ok || format <= static_cast<uint>(QImage::Format_Invalid)
        || format >= static_cast<uint>(QImage::NImageFormats)) {
        return QImage();
    }

    QImage image(width, height, static_cast<QImage::Format>(format));
    if (image.isNull()) {
        return QImage();
    }

    const qreal scale = metadata.value(QLatin1String("scale")).toReal(&ok);
    if (ok && scale > 0) {
        image.setDevicePixelRatio(scale);
    }

    QFile file;
    if (!file.open(readFd, QFileDevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        return QImage();
    }

    QDataStream stream(&file);
    const qint64 toRead = image.sizeInBytes();
    if (stream.readRawData(reinterpret_cast<char*>(image.bits()), toRead) != toRead) {
        return QImage();
    }

    return image;
}

void WindowThumbnailService::captureWindowAsync(const QString& kwinHandle, int maxSize)
{
    if (kwinHandle.isEmpty()) {
        return;
    }

    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        qCWarning(lcOverlay) << "captureWindowAsync: pipe creation failed";
        Q_EMIT captureFinished(kwinHandle, QString());
        return;
    }

    QDBusUnixFileDescriptor fd(pipeFds[1]);
    close(pipeFds[1]);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        kScreenShot2Service, kScreenShot2Path, kScreenShot2Iface,
        QStringLiteral("CaptureWindow"));
    msg << kwinHandle << QVariantMap() << QVariant::fromValue(fd);

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);

    auto* watcher = new QDBusPendingCallWatcher(call, this);
    const int readFd = pipeFds[0];

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, readFd, kwinHandle, maxSize](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<QVariantMap> reply(*w);
                if (reply.isError()) {
                    qCInfo(lcOverlay) << "captureWindowAsync:" << kwinHandle << "DBus error:"
                                     << reply.error().message();
                    close(readFd);
                    Q_EMIT captureFinished(kwinHandle, QString());
                    return;
                }

                const QVariantMap metadata = reply.value();
                QFuture<QImage> future = QtConcurrent::run(
                    [readFd, metadata, maxSize]() -> QImage {
                        QImage image = readImageFromPipe(readFd, metadata);
                        if (!image.isNull() && maxSize > 0
                            && (image.width() > maxSize || image.height() > maxSize)) {
                            image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
                        }
                        return image;
                    });

                auto* futureWatcher = new QFutureWatcher<QImage>(this);
                connect(futureWatcher, &QFutureWatcher<QImage>::finished, this,
                        [this, futureWatcher, kwinHandle]() {
                            futureWatcher->deleteLater();
                            QImage image = futureWatcher->result();
                            QString dataUrl;
                            if (!image.isNull()) {
                                QByteArray ba;
                                QBuffer buffer(&ba);
                                buffer.open(QIODevice::WriteOnly);
                                if (image.save(&buffer, "PNG")) {
                                    dataUrl = QStringLiteral("data:image/png;base64,")
                                            + QString::fromUtf8(ba.toBase64());
                                }
                            }
                            if (dataUrl.isEmpty()) {
                                qCDebug(lcOverlay) << "captureWindowAsync:" << kwinHandle
                                                   << "no thumbnail (auth/format/pipe?)";
                            }
                            Q_EMIT captureFinished(kwinHandle, dataUrl);
                        });
                futureWatcher->setFuture(future);
            });
}

} // namespace PlasmaZones

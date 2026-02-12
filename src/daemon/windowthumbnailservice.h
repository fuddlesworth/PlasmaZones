// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Captures window thumbnails via KWin ScreenShot2 D-Bus API.
 *
 * Uses kwinHandle from EffectWindow::internalId().toString() for the window handle.
 * Requires X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2 in daemon .desktop,
 * or KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 when desktop matching fails (e.g. local install).
 */
class WindowThumbnailService : public QObject
{
    Q_OBJECT

public:
    explicit WindowThumbnailService(QObject* parent = nullptr);

    bool isAvailable() const;

    /**
     * @brief Capture thumbnail asynchronously; emits captureFinished when done
     */
    void captureWindowAsync(const QString& kwinHandle, int maxSize = 256);

Q_SIGNALS:
    void captureFinished(const QString& kwinHandle, const QString& dataUrl);
};

} // namespace PlasmaZones

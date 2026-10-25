// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/platform/logging.h"
#include "core/types/constants.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QtMath>

namespace PlasmaZones {

/// Bounds on a logical scale read off the bus. The daemon answers with the
/// compositor's fractional scale, and anything outside this range is not a
/// real output and would overflow the device-pixel arithmetic downstream.
inline constexpr qreal MinPlausibleScreenScale = 0.25;
inline constexpr qreal MaxPlausibleScreenScale = 8.0;

/// The daemon's logical scale for @p screenId read out of its getScreenInfo
/// @p reply, or 0 when the call failed or answered with something no real
/// output has.
///
/// This is the per-output fractional scale the daemon's geometry sensor sees,
/// which no QScreen in the editor or the settings app can give: on Wayland
/// QScreen::devicePixelRatio is the wl_output integer buffer scale (2 for a
/// 1.25 output). A virtual screen is answered for its physical output.
///
/// @p pixelSize, when given, receives the screen's size in device pixels as
/// the daemon publishes it beside the scale (for a virtual screen, its own
/// region's share), or an invalid size when the reply carries none, as a
/// daemon older than the key answers.
///
/// The one parser for both the blocking queryDaemonScreenScale and an
/// asynchronous getScreenInfo reply, so the layout editor and the settings
/// app read the same reply with the same bounds.
inline qreal parseDaemonScreenScale(const QDBusMessage& reply, const QString& screenId, QSize* pixelSize = nullptr)
{
    if (pixelSize) {
        *pixelSize = QSize();
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return 0.0;
    }
    const QJsonObject info = QJsonDocument::fromJson(reply.arguments().constFirst().toString().toUtf8()).object();
    const qreal scale = info.value(JsonKeys::DevicePixelRatio).toDouble(0.0);
    if (!qIsFinite(scale) || scale < MinPlausibleScreenScale || scale > MaxPlausibleScreenScale) {
        qCWarning(lcScreen) << "Ignoring implausible scale" << scale << "reported for" << screenId;
        return 0.0;
    }
    if (pixelSize) {
        const QJsonObject size = info.value(JsonKeys::PixelSize).toObject();
        const QSize reported(size.value(::PhosphorZones::ZoneJsonKeys::Width).toInt(),
                             size.value(::PhosphorZones::ZoneJsonKeys::Height).toInt());
        if (reported.width() > 0 && reported.height() > 0) {
            *pixelSize = reported;
        }
    }
    return scale;
}

/// parseDaemonScreenScale over a synchronous getScreenInfo round trip. It
/// blocks the calling thread for up to the daemon call timeout, so a caller on
/// a path that repeats (every reload, every tick) fetches asynchronously
/// instead and parses the reply with parseDaemonScreenScale.
inline qreal queryDaemonScreenScale(const QString& screenId, QSize* pixelSize = nullptr)
{
    if (screenId.isEmpty()) {
        if (pixelSize) {
            *pixelSize = QSize();
        }
        return 0.0;
    }
    return parseDaemonScreenScale(
        PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::Screen,
                                                  QStringLiteral("getScreenInfo"), {screenId}),
        screenId, pixelSize);
}

} // namespace PlasmaZones

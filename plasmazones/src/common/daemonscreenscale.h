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

#include <cmath>

namespace PlasmaZones {

/// Bounds on a logical scale read off the bus. The daemon answers with the
/// compositor's fractional scale, and anything outside this range is not a
/// real output and would overflow the device-pixel arithmetic downstream.
inline constexpr qreal MinPlausibleScreenScale = 0.25;
inline constexpr qreal MaxPlausibleScreenScale = 8.0;

/// Whether @p pixelSize (device pixels) is the mode of an output whose
/// logical size is @p logical at @p scale. The one test for it: the daemon
/// accepts a compositor pixel-size report by it (ScreenReshapeTracker), and
/// the layout editor trusts the daemon's pixel size over its scale by it.
///
/// Each side may sit away from the logical side times the scale by the sum
/// of two allowances:
/// - the scale plus one pixel, because the compositor derives the logical
///   size by dividing the mode by its scale and rounding, which moves the
///   product by up to half the scale;
/// - one pixel per 240 logical pixels of the side, rounded up, because a
///   client never sees the compositor's exact scale. wp_fractional_scale_v1
///   sends it as a numerator over 120 (the protocol's preferred_scale event),
///   and Qt reads the per-surface ratio as that fraction, so the scale here
///   can sit up to 1/240 from the one the compositor divided by, and that
///   error grows with the side it multiplies.
///
/// A different mode, a rotation the logical geometry has not caught up with,
/// a coarse integer scale standing in for a fractional one, and another
/// monitor's size all land outside. False for an empty size and a
/// non-positive scale.
///
/// A monitor whose mode lands within the tolerance of another's still passes
/// for it. When one replaces another on the same connector and the QScreen
/// change arrives first, the outgoing monitor's report, still pending, is
/// checked against the incoming monitor's geometry and accepted if the two
/// are that close (a 1366x768 panel for a 1360x768 one at scale 1), and
/// stands until the incoming monitor's own report replaces it. The size, the
/// scale and the geometry are all a client sees of either monitor, so
/// nothing here can tell the two apart.
inline bool pixelSizeMatchesLogicalSize(const QSize& pixelSize, const QSize& logical, qreal scale)
{
    if (pixelSize.isEmpty() || logical.isEmpty() || !(scale > 0.0)) {
        return false;
    }
    const auto sideAgrees = [scale](int pixels, int logicalSide) {
        const qreal tolerance = scale + 1.0 + std::ceil(logicalSide / 240.0);
        return qAbs(pixels - logicalSide * scale) <= tolerance;
    };
    return sideAgrees(pixelSize.width(), logical.width()) && sideAgrees(pixelSize.height(), logical.height());
}

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
/// daemon older than the key answers. The daemon publishes the output's mode
/// as the compositor reported it, once the report agrees with the screen
/// (@ref pixelSizeMatchesLogicalSize), or at once on a screen whose scale the
/// daemon cannot measure, or until then the size last accepted for that
/// monitor, in this session or an earlier one. With neither
/// it estimates the size as the logical size times the scale, rounded, which
/// can miss the mode by a pixel on a fractional scale. The reply's
/// pixelSizeSource says which of the three the size is.
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screen_resolver.h"

#include "constants.h"

#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>

namespace PlasmaZones {

QString ScreenResolver::effectiveScreenAt(const QPoint& pos, int daemonTimeoutMs)
{
    // Ask the daemon first — it's the only component that knows about
    // virtual-screen carve-outs and disambiguates identical monitors via
    // connector-suffixed IDs.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getEffectiveScreenAt"));
    msg << pos.x() << pos.y();
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, daemonTimeoutMs);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const QString daemonId = reply.arguments().at(0).toString();
        if (!daemonId.isEmpty()) {
            return daemonId;
        }
    }

    // Daemon unreachable or returned empty — fall back to Qt's own screen
    // lookup. This misses virtual-screen contexts but is good enough for
    // the "pick a monitor to open on" use case when no daemon exists yet
    // (e.g. editor launched before the daemon has started).
    QScreen* screen = QGuiApplication::screenAt(pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->name() : QString();
}

QString ScreenResolver::effectiveScreenAtCursor(int daemonTimeoutMs)
{
    return effectiveScreenAt(QCursor::pos(), daemonTimeoutMs);
}

} // namespace PlasmaZones

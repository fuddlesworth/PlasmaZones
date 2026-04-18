// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/Resolver.h"

#include <QCursor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>

namespace Phosphor::Screens {

QString ScreenResolver::effectiveScreenAt(const QPoint& pos, const ResolverEndpoint& endpoint, int timeoutMs)
{
    // Ask the daemon first — it's the only component that knows about
    // virtual-screen carve-outs and disambiguates identical monitors via
    // connector-suffixed IDs. Skip the call entirely if the daemon isn't
    // already on the bus so we don't trigger D-Bus auto-activation as a
    // side-effect of a cursor-screen lookup (the editor can legitimately
    // run before the daemon — e.g. `plasmazones-editor --help`).
    auto bus = QDBusConnection::sessionBus();
    auto* busIface = bus.interface();
    const bool daemonOnBus = bus.isConnected() && busIface && busIface->isServiceRegistered(endpoint.service);
    if (daemonOnBus) {
        QDBusMessage msg =
            QDBusMessage::createMethodCall(endpoint.service, endpoint.path, endpoint.interfaceName, endpoint.method);
        msg.setAutoStartService(false);
        msg << pos.x() << pos.y();
        QDBusMessage reply = bus.call(msg, QDBus::Block, timeoutMs);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QString daemonId = reply.arguments().at(0).toString();
            if (!daemonId.isEmpty()) {
                return daemonId;
            }
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

QString ScreenResolver::effectiveScreenAtCursor(const ResolverEndpoint& endpoint, int timeoutMs)
{
    return effectiveScreenAt(QCursor::pos(), endpoint, timeoutMs);
}

} // namespace Phosphor::Screens

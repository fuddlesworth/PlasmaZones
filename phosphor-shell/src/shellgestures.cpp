// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellGestures.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShellGestures, "phosphorshell.gestures")

namespace PhosphorShellApp {

ShellGestures::ShellGestures(QObject* parent)
    : ShellGestures(QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath), parent)
{
}

ShellGestures::ShellGestures(const QString& service, const QString& objectPath, QObject* parent)
    : QObject(parent)
{
    // A bus-name subscription, not a proxy: it follows the daemon across
    // restarts without a watcher, because the match is on the well-known
    // name rather than a unique connection.
    const bool ok = QDBusConnection::sessionBus().connect(service, objectPath, interfaceName(), signalName(), this,
                                                          SLOT(onGestureReported(QString, QString, uint)));
    if (!ok) {
        qCWarning(lcShellGestures) << "could not subscribe to" << interfaceName() << signalName() << "on" << service;
    }
}

QString ShellGestures::interfaceName()
{
    return QString(PhosphorProtocol::Service::Interface::CompositorBridge);
}

QString ShellGestures::signalName()
{
    return QStringLiteral("gestureReported");
}

void ShellGestures::onGestureReported(const QString& kind, const QString& direction, uint fingerCount)
{
    qCDebug(lcShellGestures) << kind << direction << fingerCount;
    if (kind == QLatin1String("swipe")) {
        Q_EMIT swiped(direction, fingerCount);
    } else if (kind == QLatin1String("pinch")) {
        Q_EMIT pinched(direction, fingerCount);
    }
}

} // namespace PhosphorShellApp

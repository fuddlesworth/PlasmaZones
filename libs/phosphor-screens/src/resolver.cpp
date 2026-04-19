// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/Resolver.h"

#include "PhosphorScreens/ScreenIdentity.h"

#include <QCursor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>
#include <QTimer>

namespace Phosphor::Screens {

namespace {
// Extra wall-clock budget on top of @p timeoutMs before the hard-cap timer
// fires. D-Bus gets first-strike rights at the caller's timeout; this
// cushion only catches the pathological case where D-Bus's own deadline
// drifts. A simultaneous expiry on both paths would just be a race.
constexpr int kResolverHardCapMarginMs = 250;
} // namespace

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

        // Spin a local event loop while the D-Bus reply is outstanding
        // instead of QDBus::Block — which freezes the thread (including
        // its QPA event queue) until the reply or D-Bus's own timeout.
        // The sync-returning API is preserved; the difference is that
        // queued signals, posted events, and pending UI paints keep
        // running during the wait. A belt-and-braces QTimer caps the
        // wall-clock even if D-Bus's internal deadline drifts.
        QDBusPendingCall pending = bus.asyncCall(msg, timeoutMs);
        QDBusPendingCallWatcher watcher(pending);
        QEventLoop loop;
        QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        QTimer hardCap;
        hardCap.setSingleShot(true);
        QObject::connect(&hardCap, &QTimer::timeout, &loop, &QEventLoop::quit);
        hardCap.start(timeoutMs + kResolverHardCapMarginMs);
        // ExcludeUserInputEvents so a click / keypress that lands during the
        // wait doesn't re-enter UI handlers that could call back into the
        // resolver (editor's own screen-picker → this function → nested
        // event loop → editor again). Queued signals, timers, and D-Bus
        // traffic still dispatch, which is exactly what we need to let the
        // pending reply land.
        loop.exec(QEventLoop::ExcludeUserInputEvents);

        if (watcher.isFinished() && !watcher.isError()) {
            QDBusPendingReply<QString> reply = pending;
            if (reply.isValid()) {
                const QString daemonId = reply.value();
                if (!daemonId.isEmpty()) {
                    return daemonId;
                }
            }
        }
    }

    // Daemon unreachable or returned empty — fall back to Qt's own screen
    // lookup. This misses virtual-screen contexts but is good enough for
    // the "pick a monitor to open on" use case when no daemon exists yet
    // (e.g. editor launched before the daemon has started). Returns the
    // canonical EDID-style identifier so callers see the same ID shape
    // the daemon path produces (connector-name-only would be a different
    // format and break code that routes both through one resolver).
    QScreen* screen = QGuiApplication::screenAt(pos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? ScreenIdentity::identifierFor(screen) : QString();
}

QString ScreenResolver::effectiveScreenAtCursor(const ResolverEndpoint& endpoint, int timeoutMs)
{
    return effectiveScreenAt(QCursor::pos(), endpoint, timeoutMs);
}

} // namespace Phosphor::Screens

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kwinsendertrust.h"

#include "core/platform/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QLatin1StringView>

#include <algorithm>
#include <array>

namespace PlasmaZones {

KwinSenderTrust::KwinSenderTrust(QObject* parent)
    : QObject(parent)
{
    // Pre-warm so the first authenticated call of a session is a one-set-
    // lookup hit instead of a sync GetConnectionUnixProcessID round-trip from
    // inside a D-Bus method handler. Watcher armed so a kwin restart re-fires
    // the pre-warm; the per-sender unregistration watchers installed by
    // validateExeAndTrust handle trust eviction on the way out.
    m_kwinWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.KWin"), QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForRegistration, this);
    connect(m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
        prewarmKwinTrust();
    });
    // Initial fire covers the steady-state case where kwin came up before
    // plasmazones. If kwin is not running yet, GetNameOwner errors, the
    // pre-warm bails, and the registration callback retries when kwin lands.
    prewarmKwinTrust();
}

bool KwinSenderTrust::isTrustedSender(const QString& uniqueName, const QDBusConnection& bus)
{
    if (uniqueName.isEmpty()) {
        return true;
    }
    if (m_trusted.contains(uniqueName)) {
        return true;
    }
    // Slow-path fallback for a call that races a fresh pre-warm (the first
    // call after a kwin restart). Bounded with the shared SyncCallTimeoutMs:
    // the dbus-daemon's GetConnectionUnixProcessID is a hash lookup, so the
    // cap marks "something is wrong" rather than an expected latency, and
    // Qt's default 25 s would freeze the daemon's main thread under
    // dbus-daemon stress.
    QDBusConnection connection = bus;
    QDBusMessage pidMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    pidMsg << uniqueName;
    const QDBusMessage pidReplyMsg =
        connection.call(pidMsg, QDBus::Block, PhosphorProtocol::Service::SyncCallTimeoutMs);
    if (pidReplyMsg.type() != QDBusMessage::ReplyMessage || pidReplyMsg.arguments().isEmpty()) {
        // Most commonly the caller disconnected mid-flight (NameHasNoOwner).
        // Benign and self-healing: kwin's next registration re-fires the
        // pre-warm. Debug level; real rejections log at warning inside
        // validateExeAndTrust.
        qCDebug(lcDbus) << "KwinSenderTrust: GetConnectionUnixProcessID failed for" << uniqueName << "-"
                        << pidReplyMsg.errorMessage();
        return false;
    }
    const uint pid = pidReplyMsg.arguments().constFirst().toUInt();
    if (pid == 0) {
        return false;
    }
    return validateExeAndTrust(uniqueName, pid);
}

bool KwinSenderTrust::validateExeAndTrust(const QString& uniqueName, uint pid)
{
    if (m_trusted.contains(uniqueName)) {
        return true;
    }
    // /proc/<pid>/exe is a kernel-maintained symlink to the actual binary;
    // compare the basename against the accepted set, since the full path
    // differs by distro. The project is Wayland-only, but the X11 binary is
    // accepted too: the effect plugin runs inside whichever kwin variant the
    // user is on. kwin_wayland_wrapper is the launcher shim some distros ship;
    // without it authentication fails silently on packaged installs.
    static constexpr std::array<QLatin1StringView, 3> AcceptedExeBasenames = {
        QLatin1StringView("kwin_wayland"),
        QLatin1StringView("kwin_wayland_wrapper"),
        QLatin1StringView("kwin_x11"),
    };
    const QString exePath = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    if (exePath.isEmpty()) {
        qCWarning(lcDbus) << "KwinSenderTrust: cannot resolve /proc/" << pid << "/exe, rejecting" << uniqueName;
        return false;
    }
    const QString exeBasename = QFileInfo(exePath).fileName();
    const bool accepted =
        std::any_of(AcceptedExeBasenames.begin(), AcceptedExeBasenames.end(), [&exeBasename](QLatin1StringView v) {
            return exeBasename == v;
        });
    if (!accepted) {
        qCWarning(lcDbus) << "KwinSenderTrust: rejecting non-kwin sender" << uniqueName << "pid=" << pid
                          << "exe=" << exePath;
        return false;
    }
    // Cache the name and arm a watcher that drops it the moment the name's
    // owner disappears, so a kwin restart followed by a rapid PID reuse on a
    // process binding the same unique name cannot inherit trust.
    m_trusted.insert(uniqueName);
    auto* watcher = new QDBusServiceWatcher(uniqueName, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this, watcher](const QString& service) {
        m_trusted.remove(service);
        watcher->deleteLater();
    });
    qCDebug(lcDbus) << "KwinSenderTrust: admitted" << uniqueName << "pid=" << pid << "exe=" << exePath;
    return true;
}

void KwinSenderTrust::prewarmKwinTrust()
{
    // Async leg 1: org.kde.KWin to its unique bus name. A no-op (logged at
    // debug) when kwin is not up yet; the registration watcher retries.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                       QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    msg << QStringLiteral("org.kde.KWin");
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDbus) << "KwinSenderTrust: GetNameOwner(org.kde.KWin) failed -" << reply.error().message()
                            << "(retrying on next NameOwnerChanged)";
            return;
        }
        const QString uniqueName = reply.value();
        if (!uniqueName.isEmpty()) {
            resolvePidAndTrust(uniqueName);
        }
    });
}

void KwinSenderTrust::resolvePidAndTrust(const QString& uniqueName)
{
    if (m_trusted.contains(uniqueName)) {
        return;
    }
    // Async leg 2: unique name to PID. Both legs are asynchronous so the
    // daemon's main thread never blocks on the dbus-daemon for the pre-warm.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    msg << uniqueName;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, uniqueName](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<uint> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDbus) << "KwinSenderTrust: GetConnectionUnixProcessID failed for" << uniqueName << ":"
                            << reply.error().message();
            return;
        }
        const uint pid = reply.value();
        if (pid != 0) {
            validateExeAndTrust(uniqueName, pid);
        }
    });
}

} // namespace PlasmaZones

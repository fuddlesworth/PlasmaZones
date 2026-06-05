// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-notifications-cli: headless acceptance harness + worked
// example for the phosphor-service-notifications library. Unlike the sibling
// service CLIs (which drive their lib in a single short-lived process), this
// one is BOTH a server and a client, because org.freedesktop.Notifications is a
// service we own:
//
//   watch [--replace] [--invoke <key>]   become the daemon: own the name and
//                                         log every incoming Notify (decoded)
//                                         and every close. The acceptance test:
//                                         `notify-send hi there` from another
//                                         terminal prints here.
//   send <summary> [body] [flags]         act as a client: call Notify on the
//                                         running daemon (--app/--urgency/--icon/
//                                         --action key:label/--timeout ms).
//   close <id>                            client: call CloseNotification(id).
//   info                                  client: print GetServerInformation +
//                                         GetCapabilities.
//
// Exit codes follow the sibling CLIs: 0 ok, 64 usage, 1 runtime.
//
// Notes / honest limitations:
//  - `list` and `invoke` from a separate process are not expressible over the
//    freedesktop spec (there is no list method, and ActionInvoked is server ->
//    client). The running `watch` log IS the live view, and `--invoke <key>`
//    exercises invokeAction in-process; the unit tests cover the rest.
//  - This is a QCoreApplication, so image-data / icon-name decode that needs a
//    GUI context is not exercised here (text notifications are). The image-data
//    wire decode is covered by the milestone-8 wire test.

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

using namespace PhosphorServiceNotifications;

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

constexpr int kOk = 0;
constexpr int kUsage = 64;
constexpr int kRuntime = 1;

QString urgencyName(Notification::Urgency urgency)
{
    switch (urgency) {
    case Notification::Low:
        return QStringLiteral("low");
    case Notification::Critical:
        return QStringLiteral("critical");
    case Notification::Normal:
        break;
    }
    return QStringLiteral("normal");
}

int usage()
{
    err() << "usage: phosphor-service-notifications-cli <command>\n\n"
          << "  watch [--replace] [--invoke <key>]   run as the notification daemon and log events\n"
          << "  send <summary> [body] [--app <name>] [--urgency low|normal|critical]\n"
          << "       [--icon <name>] [--action <key:label>]... [--timeout <ms>]\n"
          << "  close <id>                            close a notification on the running daemon\n"
          << "  info                                  print the running daemon's server info + capabilities\n";
    err().flush();
    return kUsage;
}

// A QDBusInterface onto whatever daemon currently owns the name.
QDBusInterface notificationsInterface()
{
    return QDBusInterface(NotificationServer::serviceName(), NotificationServer::objectPath(),
                          NotificationServer::serviceName(), QDBusConnection::sessionBus());
}

void printNotification(NotificationServer& server, Notification* n)
{
    out() << "+ [" << n->id() << "] " << n->appName() << ": " << n->summary();
    if (!n->body().isEmpty())
        out() << " - " << n->body();
    out() << "\n    urgency=" << urgencyName(n->urgency());
    if (!n->category().isEmpty())
        out() << " category=" << n->category();
    out() << " expire=" << n->expireTimeout() << "ms";
    if (n->hasImage())
        out() << " image=" << n->image().width() << "x" << n->image().height();
    if (!n->actions().isEmpty())
        out() << " actions=[" << n->actions().join(QLatin1String(", ")) << "]";
    out() << "  (" << server.notifications().size() << " live)\n";
    out().flush();
}

int cmdWatch(bool replace, const QString& invokeKey)
{
    NotificationServer server;
    if (!server.nameAcquired() && replace)
        server.acquireName(true);
    if (!server.nameAcquired()) {
        err() << "could not acquire " << NotificationServer::serviceName()
              << " (another notification daemon owns it). Pass --replace to take over.\n";
        err().flush();
        return kRuntime;
    }

    QObject::connect(&server, &NotificationServer::notificationAdded, &server, [&](Notification* n) {
        printNotification(server, n);
        if (!invokeKey.isEmpty() && n->actions().contains(invokeKey)) {
            out() << "    -> invoking action '" << invokeKey << "' on [" << n->id() << "]\n";
            out().flush();
            server.invokeAction(n->id(), invokeKey);
        }
    });
    QObject::connect(&server, &NotificationServer::NotificationClosed, &server, [&](uint id, uint reason) {
        out() << "- [" << id << "] closed (reason " << reason << ")  (" << server.notifications().size() << " live)\n";
        out().flush();
    });

    out() << "phosphor notifications daemon listening on " << NotificationServer::serviceName()
          << " - Ctrl+C to stop\n";
    out().flush();
    return QCoreApplication::exec();
}

int cmdSend(const QStringList& args)
{
    QString summary;
    QString body;
    QString app = QStringLiteral("phosphor-cli");
    QString icon;
    int timeout = -1;
    QVariantMap hints;
    QStringList actions;

    QStringList positional;
    bool missingValue = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args.at(i);
        auto next = [&]() -> QString {
            // A flag with no following token is a usage error: flag it rather
            // than silently substituting an empty value (which --app / --action
            // would otherwise accept as a blank app name / empty action).
            if (i + 1 >= args.size()) {
                missingValue = true;
                return QString();
            }
            return args.at(++i);
        };
        if (a == QLatin1String("--app")) {
            app = next();
        } else if (a == QLatin1String("--icon")) {
            icon = next();
        } else if (a == QLatin1String("--timeout")) {
            bool ok = false;
            timeout = next().toInt(&ok);
            if (!ok)
                return usage();
        } else if (a == QLatin1String("--urgency")) {
            const QString u = next();
            uchar level = 1;
            if (u == QLatin1String("low"))
                level = 0;
            else if (u == QLatin1String("critical"))
                level = 2;
            else if (u == QLatin1String("normal"))
                level = 1;
            else
                return usage();
            hints.insert(QStringLiteral("urgency"), QVariant::fromValue(level));
        } else if (a == QLatin1String("--action")) {
            const QString spec = next();
            const int colon = spec.indexOf(QLatin1Char(':'));
            const QString key = colon >= 0 ? spec.left(colon) : spec;
            const QString label = colon >= 0 ? spec.mid(colon + 1) : spec;
            actions << key << label;
        } else if (a.startsWith(QLatin1String("--"))) {
            return usage();
        } else {
            positional << a;
        }
    }

    if (missingValue)
        return usage();
    if (positional.isEmpty())
        return usage();
    summary = positional.at(0);
    if (positional.size() > 1)
        body = positional.at(1);

    QDBusInterface iface = notificationsInterface();
    if (!iface.isValid()) {
        err() << "no notification daemon on the session bus\n";
        err().flush();
        return kRuntime;
    }
    QDBusReply<uint> reply =
        iface.call(QStringLiteral("Notify"), app, 0u, icon, summary, body, actions, hints, timeout);
    if (!reply.isValid()) {
        err() << "Notify failed: " << reply.error().message() << "\n";
        err().flush();
        return kRuntime;
    }
    out() << "sent, id " << reply.value() << "\n";
    out().flush();
    return kOk;
}

int cmdClose(const QString& idText)
{
    bool ok = false;
    const uint id = idText.toUInt(&ok);
    if (!ok)
        return usage();
    QDBusInterface iface = notificationsInterface();
    if (!iface.isValid()) {
        err() << "no notification daemon on the session bus\n";
        err().flush();
        return kRuntime;
    }
    QDBusReply<void> reply = iface.call(QStringLiteral("CloseNotification"), id);
    if (!reply.isValid()) {
        err() << "CloseNotification failed: " << reply.error().message() << "\n";
        err().flush();
        return kRuntime;
    }
    out() << "closed " << id << "\n";
    out().flush();
    return kOk;
}

int cmdInfo()
{
    QDBusInterface iface = notificationsInterface();
    if (!iface.isValid()) {
        err() << "no notification daemon on the session bus\n";
        err().flush();
        return kRuntime;
    }
    const QDBusMessage info = iface.call(QStringLiteral("GetServerInformation"));
    if (info.type() == QDBusMessage::ErrorMessage) {
        err() << "GetServerInformation failed: " << info.errorMessage() << "\n";
        err().flush();
        return kRuntime;
    }
    const QVariantList a = info.arguments();
    if (a.size() != 4) {
        err() << "GetServerInformation returned an unexpected reply (" << a.size() << " args)\n";
        err().flush();
        return kRuntime;
    }
    out() << "server: " << a.at(0).toString() << " (" << a.at(1).toString() << ") version " << a.at(2).toString()
          << ", spec " << a.at(3).toString() << "\n";
    QDBusReply<QStringList> caps = iface.call(QStringLiteral("GetCapabilities"));
    if (caps.isValid())
        out() << "capabilities: " << caps.value().join(QLatin1String(", ")) << "\n";
    out().flush();
    return kOk;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    // Default to watch when no subcommand is given.
    const QString command = args.size() > 1 ? args.at(1) : QStringLiteral("watch");
    const QStringList rest = args.mid(2);

    if (command == QLatin1String("watch")) {
        bool replace = false;
        QString invokeKey;
        for (int i = 0; i < rest.size(); ++i) {
            if (rest.at(i) == QLatin1String("--replace"))
                replace = true;
            else if (rest.at(i) == QLatin1String("--invoke") && i + 1 < rest.size())
                invokeKey = rest.at(++i);
            else
                return usage();
        }
        return cmdWatch(replace, invokeKey);
    }
    if (command == QLatin1String("send"))
        return cmdSend(rest);
    if (command == QLatin1String("close"))
        return rest.isEmpty() ? usage() : cmdClose(rest.at(0));
    if (command == QLatin1String("info"))
        return cmdInfo();
    if (command == QLatin1String("help") || command == QLatin1String("--help") || command == QLatin1String("-h")) {
        usage(); // print the usage text
        return kOk; // an explicit help request is a success, not a usage error
    }

    return usage();
}

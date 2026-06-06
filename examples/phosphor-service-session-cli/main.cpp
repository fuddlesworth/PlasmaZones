// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-session-cli: headless acceptance harness + worked example
// for the phosphor-service-session library. Drives the lib directly against the
// real system-bus logind, the same pattern as the sibling phosphor-service-*
// CLIs.
//
// The capability queries and the session-path lookup resolve asynchronously, so
// every command pumps the event loop for a settle window before reading or
// acting. interactive=false is set (a dev shell has no polkit agent, so logind
// returns an auth error rather than prompting). The power actions are real: this
// is the acceptance harness, so `suspend` suspends and `power-off` powers off.

#include <PhosphorServiceSession/SessionHost.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>
#include <QTextStream>
#include <QTimer>

#include <functional>

using namespace PhosphorServiceSession;

namespace {

// sysexits-style return codes.
constexpr int kOk = 0;
constexpr int kFailure = 1;
constexpr int kUsage = 64;

// Ceiling for the async capability + session-path lookups. logind answers on the
// local system bus quickly; we return as soon as the answers land (see
// pumpUntil) and only wait this long if the bus is slow or silent.
constexpr int kSettleMs = 1200;
// Flush window after a fire-and-forget action so the call reaches logind before
// the process exits.
constexpr int kFlushMs = 400;

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Pump the event loop until @p done() is satisfied or @p ceilingMs elapses,
// whichever comes first. Driving the settle off the capability replies (rather
// than a fixed sleep) returns immediately on a fast bus while the ceiling still
// bounds a slow or silent logind.
void pumpUntil(SessionHost& host, int ceilingMs, const std::function<bool()>& done)
{
    if (done())
        return;
    QEventLoop loop;
    QTimer ceiling;
    ceiling.setSingleShot(true);
    QObject::connect(&ceiling, &QTimer::timeout, &loop, &QEventLoop::quit);
    ceiling.start(ceilingMs);
    QObject::connect(&host, &SessionHost::capabilitiesChanged, &loop, [&] {
        if (done())
            loop.quit();
    });
    loop.exec();
}

bool capabilitiesResolved(const SessionHost& host)
{
    using A = SessionHost::Availability;
    return host.canPowerOff() != A::Unknown && host.canReboot() != A::Unknown && host.canHalt() != A::Unknown
        && host.canSuspend() != A::Unknown && host.canHibernate() != A::Unknown && host.canHybridSleep() != A::Unknown
        && host.canSuspendThenHibernate() != A::Unknown;
}

QString availabilityName(SessionHost::Availability availability)
{
    switch (availability) {
    case SessionHost::Availability::Yes:
        return QStringLiteral("yes");
    case SessionHost::Availability::No:
        return QStringLiteral("no");
    case SessionHost::Availability::NotApplicable:
        return QStringLiteral("na");
    case SessionHost::Availability::Challenge:
        return QStringLiteral("challenge");
    case SessionHost::Availability::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

bool actionable(SessionHost::Availability availability)
{
    return availability == SessionHost::Availability::Yes || availability == SessionHost::Availability::Challenge;
}

void printStatus(QTextStream& out, const SessionHost& host)
{
    out << "session capabilities:\n";
    out << "  power-off               " << availabilityName(host.canPowerOff()) << '\n';
    out << "  reboot                  " << availabilityName(host.canReboot()) << '\n';
    out << "  halt                    " << availabilityName(host.canHalt()) << '\n';
    out << "  suspend                 " << availabilityName(host.canSuspend()) << '\n';
    out << "  hibernate               " << availabilityName(host.canHibernate()) << '\n';
    out << "  hybrid-sleep            " << availabilityName(host.canHybridSleep()) << '\n';
    out << "  suspend-then-hibernate  " << availabilityName(host.canSuspendThenHibernate()) << '\n';
    out.flush();
}

void printUsage(QTextStream& out)
{
    out << "usage: phosphor-service-session-cli <command>\n"
        << "  status                  print logind session capabilities (default)\n"
        << "  lock                    lock this session via logind\n"
        << "  logout                  end this session (graceful request, then terminate)\n"
        << "  suspend                 suspend to RAM\n"
        << "  hibernate               hibernate to disk\n"
        << "  hybrid-sleep            suspend to both RAM and disk\n"
        << "  suspend-then-hibernate  suspend, then hibernate after a delay\n"
        << "  reboot                  reboot the system\n"
        << "  power-off               power off the system\n"
        << "  halt                    halt the system\n";
    out.flush();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString command = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("status");

    if (argc > 2) {
        err << "unexpected extra arguments after '" << command << "'\n\n";
        err.flush();
        printUsage(out);
        return kUsage;
    }

    if (command == QLatin1String("-h") || command == QLatin1String("--help") || command == QLatin1String("help")) {
        printUsage(out);
        return kOk;
    }

    SessionHost host;
    // A dev shell has no polkit agent, so prefer the non-interactive path:
    // logind returns an auth error rather than waiting on a prompt nobody answers.
    host.setInteractive(false);

    // Let the capability + session-path lookups resolve before acting on them.
    // Returns as soon as logind has answered every Can* query, bounded by
    // kSettleMs. (The session-path lookup has no signal to wait on; it is issued
    // alongside the capability calls and has resolved by the time they all have,
    // and lock() falls back to LockSessions() if it has not.)
    pumpUntil(host, kSettleMs, [&] {
        return capabilitiesResolved(host);
    });

    if (command == QLatin1String("status")) {
        printStatus(out, host);
        return kOk;
    }

    if (command == QLatin1String("lock")) {
        host.lock();
        out << "lock requested\n";
        out.flush();
        pump(kFlushMs);
        return kOk;
    }

    if (command == QLatin1String("logout")) {
        // logout() emits logoutRequested() for the shell to end the session
        // gracefully; with no shell here, fall back to the logind terminate.
        QObject::connect(&host, &SessionHost::logoutRequested, &app, [&] {
            out << "logout requested; terminating session via logind\n";
            out.flush();
            host.terminateSession();
        });
        host.logout();
        pump(kFlushMs);
        return kOk;
    }

    // Capability-gated power actions: report unavailability with a non-zero exit
    // rather than firing a call logind would reject.
    struct Action
    {
        QLatin1String name;
        SessionHost::Availability capability;
        void (SessionHost::*invoke)();
    };
    const Action actions[] = {
        {QLatin1String("suspend"), host.canSuspend(), &SessionHost::suspend},
        {QLatin1String("hibernate"), host.canHibernate(), &SessionHost::hibernate},
        {QLatin1String("hybrid-sleep"), host.canHybridSleep(), &SessionHost::hybridSleep},
        {QLatin1String("suspend-then-hibernate"), host.canSuspendThenHibernate(), &SessionHost::suspendThenHibernate},
        {QLatin1String("reboot"), host.canReboot(), &SessionHost::reboot},
        {QLatin1String("power-off"), host.canPowerOff(), &SessionHost::powerOff},
        {QLatin1String("halt"), host.canHalt(), &SessionHost::halt},
    };
    for (const Action& action : actions) {
        if (command != action.name)
            continue;
        if (!actionable(action.capability)) {
            err << action.name << " is not available (logind reports: " << availabilityName(action.capability) << ")\n";
            err.flush();
            return kFailure;
        }
        (host.*action.invoke)();
        out << action.name << " requested\n";
        out.flush();
        pump(kFlushMs);
        return kOk;
    }

    err << "unknown command: " << command << "\n\n";
    err.flush();
    printUsage(out);
    return kUsage;
}

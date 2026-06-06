// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-lock-cli: headless driver + worked example for the
// phosphor-service-lock library.
//
//   authenticate [user]   Verify a password for the user (default: the current
//                         user) through PAM and print success/failure. Needs no
//                         compositor, so it runs anywhere. --service NAME selects
//                         the /etc/pam.d stack (default "login").
//   supported             Report whether the compositor advertises
//                         ext-session-lock-v1, queried under the live
//                         phosphorwayland QPA.
//
// There is deliberately no interactive "lock" command yet: locking the session
// without the per-output lock surfaces (a Phase 4 shell concern) would blank the
// outputs with no visible password prompt. Until the surfaces land, the lock
// lifecycle is exercised by the unit tests; this CLI covers the authentication
// path the plan's acceptance criterion calls for.

#include <PhosphorServiceLock/LockService.h>
#include <PhosphorServiceLock/PamAuthenticator.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <pwd.h>
#include <termios.h>
#include <unistd.h>

using namespace PhosphorServiceLock;

namespace {

// sysexits-style return codes.
constexpr int kOk = 0;
constexpr int kFailure = 1;
constexpr int kUsage = 64;

QString currentUser()
{
    if (const struct passwd* pw = getpwuid(getuid()); pw && pw->pw_name)
        return QString::fromLocal8Bit(pw->pw_name);
    return qEnvironmentVariable("USER");
}

// Read one line from stdin with terminal echo disabled when stdin is a TTY, so
// the password is never shown. Falls back to a plain read when stdin is piped.
QString readPassword(QTextStream& out, const QString& prompt)
{
    out << prompt;
    out.flush();

    const bool tty = ::isatty(STDIN_FILENO) == 1;
    struct termios saved{};
    if (tty) {
        ::tcgetattr(STDIN_FILENO, &saved);
        struct termios hidden = saved;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
    }

    QTextStream in(stdin);
    const QString line = in.readLine();

    if (tty) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        out << '\n';
        out.flush();
    }
    return line;
}

int runAuthenticate(int argc, char** argv, const QString& service, const QString& requestedUser)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString username = requestedUser.isEmpty() ? currentUser() : requestedUser;
    if (username.isEmpty()) {
        err << "could not determine the user to authenticate\n";
        return kUsage;
    }

    PamAuthenticator authenticator(service);
    const QString password = readPassword(out, QStringLiteral("Password for %1: ").arg(username));

    int rc = kFailure;
    QObject::connect(&authenticator, &PamAuthenticator::succeeded, &app, [&] {
        out << "authentication succeeded\n";
        out.flush();
        rc = kOk;
        app.quit();
    });
    QObject::connect(&authenticator, &PamAuthenticator::failed, &app, [&](const QString& reason) {
        out << "authentication failed: " << reason << '\n';
        out.flush();
        rc = kFailure;
        app.quit();
    });

    authenticator.authenticate(username, password);
    app.exec();
    return rc;
}

int runSupported(int argc, char** argv)
{
    // Bring up the phosphorwayland QPA so the session-lock global can be detected.
    PhosphorWayland::registerLayerShellPlugin();
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);

    LockService service;
    const bool supported = service.isSupported();
    out << "ext-session-lock-v1 supported: " << (supported ? "yes" : "no") << '\n';
    out.flush();
    return supported ? kOk : kFailure;
}

void printUsage(QTextStream& out)
{
    out << "usage: phosphor-service-lock-cli <command> [args]\n"
        << "  authenticate [user] [--service NAME]  verify a password through PAM (default user: current)\n"
        << "  supported                             report ext-session-lock-v1 availability\n";
    out.flush();
}

} // namespace

int main(int argc, char** argv)
{
    // Inspect the command before constructing the application: `authenticate`
    // needs no compositor (QCoreApplication), `supported` runs under the Wayland
    // QPA (QGuiApplication + registerLayerShellPlugin, which must precede it).
    const QString command = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("authenticate");

    QTextStream out(stdout);
    if (command == QLatin1String("-h") || command == QLatin1String("--help") || command == QLatin1String("help")) {
        printUsage(out);
        return kOk;
    }

    if (command == QLatin1String("supported"))
        return runSupported(argc, argv);

    if (command == QLatin1String("authenticate")) {
        // Parse the remaining args: an optional positional user and --service NAME.
        QString service = QStringLiteral("login");
        QString user;
        for (int i = 2; i < argc; ++i) {
            const QString arg = QString::fromLocal8Bit(argv[i]);
            if (arg == QLatin1String("--service") && i + 1 < argc) {
                service = QString::fromLocal8Bit(argv[++i]);
            } else if (!arg.startsWith(QLatin1Char('-')) && user.isEmpty()) {
                user = arg;
            } else {
                QTextStream(stderr) << "unrecognised argument: " << arg << '\n';
                return kUsage;
            }
        }
        return runAuthenticate(argc, argv, service, user);
    }

    QTextStream(stderr) << "unknown command: " << command << "\n\n";
    printUsage(out);
    return kUsage;
}

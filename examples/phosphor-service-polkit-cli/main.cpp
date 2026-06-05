// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-polkit-cli: headless acceptance harness + worked example for
// the phosphor-service-polkit library. It runs as the session's PolicyKit
// authentication agent: it registers, logs each authentication request, drives
// the PAM conversation, and answers the prompts.
//
// Trigger a request from another terminal with e.g. `pkexec true`. This is the
// only way to exercise the full register -> request -> prompt -> respond ->
// completed path, since it requires a live polkitd + PAM calling back into us.
//
//   --password <pw>   DEMO ONLY: auto-answer every PAM prompt with <pw>. The
//                     password is visible in the process list, so this exists
//                     purely to make the demo scriptable; a real shell collects
//                     the secret in a secure dialog. Without it, prompts are read
//                     from stdin (terminal echo suppressed for secret prompts).
//
// Exactly one agent serves a session. If your desktop already runs one (KDE /
// GNOME), registration fails and the demo prints guidance; stop that agent first
// to test.

#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>

#include <QCoreApplication>
#include <QObject>
#include <QScopeGuard>
#include <QStringList>
#include <QTextStream>

#include <termios.h>

#include <cstdio>

using namespace PhosphorServicePolkit;

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

int usage()
{
    err() << "usage: phosphor-service-polkit-cli [--password <pw>]\n\n"
          << "  Registers as the session's PolicyKit authentication agent and answers the\n"
          << "  PAM prompts for each request. Trigger one with e.g. `pkexec true` from\n"
          << "  another terminal.\n\n"
          << "  --password <pw>   DEMO ONLY: auto-answer every prompt with <pw> (visible in\n"
          << "                    the process list). Without it, prompts are read from stdin\n"
          << "                    (terminal echo suppressed for secret prompts).\n\n"
          << "  Exactly one agent serves a session; if your desktop already runs one\n"
          << "  (KDE / GNOME) registration fails. Stop it first to test this demo.\n";
    err().flush();
    return kUsage;
}

// Read one line from stdin. When @p echo is false (a secret), terminal echo is
// suppressed so the password is not printed to the screen, honouring the
// AuthRequest::echo flag. NOTE: this blocks the event loop until the user
// answers, so a cancellation arriving mid-input is not processed until Enter;
// the agent's respond() safely no-ops if the request was settled meanwhile.
QString readResponse(bool echo)
{
    QTextStream in(stdin);
    if (echo)
        return in.readLine();

    const int fd = fileno(stdin);
    termios original{};
    if (tcgetattr(fd, &original) != 0)
        return in.readLine(); // not a tty: nothing to suppress

    termios masked = original;
    masked.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(fd, TCSANOW, &masked);
    // Restore the terminal on every exit path so a future early return can never
    // leave echo disabled.
    const auto restore = qScopeGuard([fd, &original] {
        tcsetattr(fd, TCSANOW, &original);
    });
    const QString line = in.readLine();
    out() << "\n"; // the user's (un-echoed) newline
    out().flush();
    return line;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    QString password;
    bool havePassword = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--password") && i + 1 < args.size()) {
            password = args.at(++i);
            havePassword = true;
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            usage();
            return kOk;
        } else {
            return usage();
        }
    }

    auto* agent = new PolkitAgent(&app);
    if (!agent->registerAgent()) {
        err() << "could not register as the authentication agent: another agent owns this\n"
              << "session. Stop your desktop's polkit agent first to test this demo.\n";
        err().flush();
        return kRuntime;
    }
    out() << "phosphor polkit agent registered at " << PolkitAgent::defaultObjectPath() << "\n"
          << "trigger an action (e.g. `pkexec true`) from another terminal. Ctrl+C to stop.\n";
    out().flush();

    QObject::connect(agent, &PolkitAgent::authenticationRequested, &app, [agent](AuthRequest* request) {
        out() << "+ auth request: action=" << request->actionId() << "\n"
              << "    message: " << request->message() << "\n"
              << "    identities: " << request->identities().join(QLatin1String(", ")) << "\n";
        out().flush();
        // Start the PAM conversation for the (default-selected) identity; its
        // prompts arrive via promptRequested below.
        agent->authenticate();
    });

    // PAM asked for input. promptRequested is an event (fires per prompt, incl.
    // a same-text retry after a wrong answer), so answering it here is exactly
    // once per request.
    QObject::connect(agent, &PolkitAgent::promptRequested, &app,
                     [agent, havePassword, password](const QString& prompt, bool echo) {
                         out() << "    prompt: " << prompt;
                         out().flush();
                         QString response;
                         if (havePassword) {
                             response = password;
                             out() << "(answered from --password)\n";
                             out().flush();
                         } else {
                             response = readResponse(echo);
                         }
                         // Straight to PAM; the response is not retained here.
                         agent->respond(response);
                     });

    QObject::connect(agent, &PolkitAgent::authenticationCompleted, &app, [](bool gained) {
        out() << (gained ? "- authenticated\n" : "- authentication failed\n");
        out().flush();
    });
    QObject::connect(agent, &PolkitAgent::authenticationError, &app, [](const QString& text) {
        out() << "    error: " << text << "\n";
        out().flush();
    });
    QObject::connect(agent, &PolkitAgent::authenticationInfo, &app, [](const QString& text) {
        out() << "    info: " << text << "\n";
        out().flush();
    });
    QObject::connect(agent, &PolkitAgent::authenticationCancelled, &app, []() {
        out() << "- request cancelled\n";
        out().flush();
    });

    return app.exec();
}

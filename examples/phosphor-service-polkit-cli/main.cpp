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
//                     the secret in a secure dialog. Without it, prompts are
//                     read from stdin.
//
// Exactly one agent serves a session. If your desktop already runs one (KDE /
// GNOME), registration fails and the demo prints guidance; stop that agent first
// to test.

#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>

#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <QTextStream>

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
          << "                    the process list). Without it, prompts are read from stdin.\n\n"
          << "  Exactly one agent serves a session; if your desktop already runs one\n"
          << "  (KDE / GNOME) registration fails. Stop it first to test this demo.\n";
    err().flush();
    return kUsage;
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

    QObject::connect(
        agent, &PolkitAgent::authenticationRequested, &app, [agent, havePassword, password](AuthRequest* request) {
            out() << "+ auth request: action=" << request->actionId() << "\n"
                  << "    message: " << request->message() << "\n"
                  << "    identities: " << request->identities().join(QLatin1String(", ")) << "\n";
            out().flush();

            // Print and answer each PAM prompt as the conversation progresses.
            QObject::connect(request, &AuthRequest::promptChanged, agent, [agent, havePassword, password, request]() {
                const QString prompt = request->prompt();
                if (prompt.isEmpty())
                    return;
                out() << "    prompt: " << prompt;
                out().flush();

                QString response;
                if (havePassword) {
                    response = password;
                    out() << "(answered from --password)\n";
                } else {
                    QTextStream in(stdin);
                    response = in.readLine();
                }
                out().flush();
                // Straight to PAM; the response is not retained here.
                agent->respond(response);
            });

            // Start the PAM conversation for the (default-selected) identity.
            agent->authenticate();
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

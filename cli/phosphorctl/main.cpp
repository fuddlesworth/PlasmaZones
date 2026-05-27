// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphorctl — typed JSON-over-Unix-socket CLI for phosphor-shell.
// Subcommand dispatcher: top-level QCommandLineParser identifies
// `call`, `list`, `schema`, `subscribe` and hands off to the
// matching handler in subcommand_*.cpp. Each handler builds its own
// nested QCommandLineParser for the subcommand-specific flags.
//
// Phase 1.4 deliverable per docs/phosphor-shell-design/04-implementation-plan.md.

#include "client.h"
#include "subcommands.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

namespace {

void printUsage(QTextStream& out)
{
    out << "usage: phosphorctl [--socket PATH] <subcommand> [args]\n\n"
        << "Subcommands:\n"
        << "  call <target>.<fn> [--arg name=value ...]\n"
        << "      Invoke a function on a target and print its return value.\n"
        << "  list\n"
        << "      Print every registered target id, one per line.\n"
        << "  schema <target>\n"
        << "      Pretty-print the target's JSON Schema.\n"
        << "  subscribe <target>.<signal>\n"
        << "      Stream JSON events for a signal until Ctrl+C.\n\n"
        << "Socket resolution priority: --socket > $PHOSPHOR_SOCKET > $XDG_RUNTIME_DIR/phosphor.sock\n";
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("phosphorctl"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QTextStream out(stdout);
    QTextStream err(stderr);

    // Strip program name, leave [args] starting with whatever came
    // first on the command line (a subcommand, --help, --socket,
    // etc.). We do the top-level parse ourselves rather than ask
    // QCommandLineParser to handle subcommands because Qt's parser
    // treats positional args weakly and we want strict subcommand
    // dispatch.
    QStringList args = app.arguments();
    args.removeFirst();

    // Extract --socket / -s flag from the head of the arg list. The
    // flag may appear before OR after the subcommand name — we
    // handle the simpler "before" case here and let the per-
    // subcommand parser pick it up too if it shows up after.
    QString socketPathCli;
    while (!args.isEmpty()) {
        const QString& head = args.front();
        if (head == QLatin1String("--socket") || head == QLatin1String("-s")) {
            if (args.size() < 2) {
                err << "phosphorctl: --socket requires an argument\n";
                return 1;
            }
            args.removeFirst();
            socketPathCli = args.takeFirst();
        } else if (head.startsWith(QLatin1String("--socket="))) {
            socketPathCli = args.takeFirst().mid(QStringLiteral("--socket=").size());
        } else {
            break;
        }
    }

    if (args.isEmpty() || args.front() == QLatin1String("--help") || args.front() == QLatin1String("-h")
        || args.front() == QLatin1String("help")) {
        printUsage(out);
        return args.isEmpty() ? 1 : 0;
    }
    if (args.front() == QLatin1String("--version") || args.front() == QLatin1String("-v")) {
        out << QCoreApplication::applicationName() << " " << QCoreApplication::applicationVersion() << "\n";
        return 0;
    }

    const QString subcommand = args.takeFirst();
    const QString socketPath = Phosphorctl::Client::resolveSocketPath(socketPathCli);

    if (subcommand == QLatin1String("call")) {
        return Phosphorctl::runCall(args, socketPath);
    }
    if (subcommand == QLatin1String("list")) {
        return Phosphorctl::runList(args, socketPath);
    }
    if (subcommand == QLatin1String("schema")) {
        return Phosphorctl::runSchema(args, socketPath);
    }
    if (subcommand == QLatin1String("subscribe")) {
        return Phosphorctl::runSubscribe(args, socketPath);
    }

    err << "phosphorctl: unknown subcommand '" << subcommand << "'\n";
    printUsage(err);
    return 1;
}

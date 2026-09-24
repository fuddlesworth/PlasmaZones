// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-idle-cli: headless driver + worked example for the
// phosphor-service-idle library.
//
// It registers one or more idle stages and logs each as it fires, with a
// wall-clock timestamp (the "fired-at log"). Leave the keyboard and mouse alone
// to watch stages fire in timeout order; touch input to watch the ladder reset.
// With --inhibit-for it holds an idle inhibition for a while at startup, during
// which the ladder is disarmed and nothing fires.
//
// It runs under the phosphorwayland QPA shell integration (registered before
// QGuiApplication) so the ext-idle-notify-v1 protocol is live. Without a
// compositor advertising it, the demo reports unsupported and exits.

#include <PhosphorServiceIdle/IdleService.h>

#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <QGuiApplication>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QVariantMap>

using namespace PhosphorServiceIdle;

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

QString stamp()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

int usage()
{
    err() << "usage: phosphor-service-idle-cli [--stage NAME:SECONDS]... [--inhibit-for SECONDS]\n\n"
          << "  Registers idle timeouts and logs each stage as it fires, with a wall-clock\n"
          << "  timestamp. Leave input idle to watch stages fire; touch it to watch them reset.\n\n"
          << "  --stage NAME:SECONDS   Add an idle stage firing after SECONDS of inactivity\n"
          << "                         (repeatable; sorted by timeout). Defaults to a single\n"
          << "                         'idle' stage at 5s when none are given.\n"
          << "  --inhibit-for SECONDS  Hold an idle inhibition for SECONDS at startup, then\n"
          << "                         release it. The ladder is disarmed while inhibited.\n";
    err().flush();
    return kUsage;
}

QVariantMap makeStage(const QString& name, int timeoutMs)
{
    QVariantMap stage;
    stage.insert(QLatin1String("name"), name);
    stage.insert(QLatin1String("timeoutMs"), timeoutMs);
    return stage;
}
} // namespace

int main(int argc, char** argv)
{
    // MUST run before QGuiApplication: selects the phosphorwayland Wayland
    // shell-integration plugin so the ext-idle-notify-v1 global is reachable.
    PhosphorWayland::registerLayerShellPlugin();
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();

    QVariantList stages;
    int inhibitForSeconds = 0;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--stage") && i + 1 < args.size()) {
            const QString spec = args.at(++i);
            const int colon = spec.lastIndexOf(QLatin1Char(':'));
            if (colon <= 0) {
                err() << "invalid --stage (expected NAME:SECONDS): " << spec << "\n";
                return usage();
            }
            bool ok = false;
            const int seconds = spec.mid(colon + 1).toInt(&ok);
            if (!ok || seconds <= 0) {
                err() << "invalid --stage seconds (expected a positive integer): " << spec << "\n";
                return usage();
            }
            stages.append(makeStage(spec.left(colon), seconds * 1000));
        } else if (a == QLatin1String("--inhibit-for") && i + 1 < args.size()) {
            bool ok = false;
            inhibitForSeconds = args.at(++i).toInt(&ok);
            if (!ok || inhibitForSeconds <= 0) {
                err() << "invalid --inhibit-for seconds (expected a positive integer)\n";
                return usage();
            }
        } else if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            usage();
            return kOk;
        } else {
            return usage();
        }
    }

    if (stages.isEmpty())
        stages.append(makeStage(QStringLiteral("idle"), 5000));

    auto* service = new IdleService(&app);
    if (!service->isSupported()) {
        err() << "the compositor does not advertise ext-idle-notify-v1; cannot monitor idle.\n"
              << "run this inside a Phosphor / wlroots-style Wayland session.\n";
        err().flush();
        return kRuntime;
    }

    service->setStages(stages);

    out() << "[" << stamp() << "] watching " << service->stages().size() << " idle stage(s):\n";
    const QVariantList configured = service->stages();
    for (const QVariant& entry : configured) {
        const QVariantMap stage = entry.toMap();
        out() << "    " << stage.value(QLatin1String("name")).toString() << " @ "
              << (stage.value(QLatin1String("timeoutMs")).toInt() / 1000) << "s\n";
    }
    out() << "  leave input idle to fire stages; touch it to reset. Ctrl+C to stop.\n";
    out().flush();

    QObject::connect(service, &IdleService::idled, &app, [service](int stage) {
        out() << "[" << stamp() << "] idle stage " << stage << " (" << service->currentStageName() << ") reached\n";
        out().flush();
    });
    QObject::connect(service, &IdleService::resumed, &app, []() {
        out() << "[" << stamp() << "] resumed (active)\n";
        out().flush();
    });

    if (inhibitForSeconds > 0) {
        const int cookie = service->inhibit();
        out() << "[" << stamp() << "] inhibiting idle for " << inhibitForSeconds << "s (cookie " << cookie << ")\n";
        out().flush();
        QTimer::singleShot(inhibitForSeconds * 1000, &app, [service, cookie] {
            service->release(cookie);
            out() << "[" << stamp() << "] released inhibition (cookie " << cookie << ")\n";
            out().flush();
        });
    }

    return app.exec();
}

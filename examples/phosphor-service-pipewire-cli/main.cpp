// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QCoreApplication>
#include <QObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// Settle timeouts (ms). Tuned for a developer host with a local
// PipeWire daemon: handshake + initial registry walk completes well
// under 250 ms; param echoes after a write land within ~80 ms; metadata
// writes round-trip through WirePlumber within ~150 ms. Override via
// PHOSPHOR_PW_CONNECT_TIMEOUT_MS if a slow boot path is expected.
constexpr int kDefaultPostReadSettleMs = 80;
constexpr int kDefaultPostWriteSettleMs = 120;
constexpr int kDefaultPostMetadataSettleMs = 150;
constexpr int kDefaultConnectTimeoutMs = 2000;

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

int usage()
{
    err() << "usage: phosphor-service-pipewire-cli <command> [args]\n"
          << "\n"
          << "commands:\n"
          << "  list sinks                          list every Audio/Sink node\n"
          << "  list sources                        list every Audio/Source node\n"
          << "  list streams                        list every audio stream node\n"
          << "  default                             print current default sink + source names\n"
          << "  set-volume <target> <pct>           set every channel of <target> to <pct> (0..100)\n"
          << "  mute <target>                       mute node\n"
          << "  unmute <target>                     unmute node\n"
          << "  set-default-sink <name>             write default.configured.audio.sink\n"
          << "  set-default-source <name>           write default.configured.audio.source\n"
          << "\n"
          << "  <target> is one of:\n"
          << "    - a numeric PipeWire id (from `list sinks`)\n"
          << "    - a node.name string (e.g. alsa_output.pci-0000_00_1f.3.iec958-stereo)\n"
          << "    - `default.audio.sink` or `default.audio.source` for the current default\n"
          << "\n"
          << "  PHOSPHOR_PW_CONNECT_TIMEOUT_MS overrides the daemon-connect timeout\n"
          << "  (default 2000 ms). Use a larger value if the daemon is slow to start.\n";
    return 64; // EX_USAGE
}

/// Connect-timeout sourced from the env var so users hitting slow-boot
/// scenarios can override without recompiling.
int connectTimeoutMs()
{
    const QByteArray override = qgetenv("PHOSPHOR_PW_CONNECT_TIMEOUT_MS");
    if (override.isEmpty())
        return kDefaultConnectTimeoutMs;
    bool ok = false;
    const int v = override.toInt(&ok);
    return (ok && v > 0) ? v : kDefaultConnectTimeoutMs;
}

/// Wait for the connection to either reach the connected state or to
/// time out. The CLI is a one-shot tool: the daemon either answers
/// promptly or we bail with an error.
bool waitForConnect(PhosphorServicePipeWire::PipeWireConnection& conn, int timeoutMs)
{
    if (conn.isConnected())
        return true;
    QEventLoop loop;
    QObject::connect(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged, &loop, [&loop, &conn]() {
        if (conn.isConnected())
            loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return conn.isConnected();
}

/// Many commands need the registry to settle (initial node enumeration)
/// before they can act. PipeWire fires every `global` event before the
/// `done` that completes the connected handshake, but our connect path
/// flips `connected` on the done event, so by the time we observe
/// connected the nodes are already enumerated. Still, give a short
/// extra tick for late-arriving SPA_PARAM_Props events (volumes,
/// mute) before we render output.
void settleRegistry(int extraMs = kDefaultPostReadSettleMs)
{
    QEventLoop loop;
    QTimer::singleShot(extraMs, &loop, &QEventLoop::quit);
    loop.exec();
}

PhosphorServicePipeWire::PwNode* findNode(PhosphorServicePipeWire::PipeWireConnection& conn, uint id)
{
    for (auto* node : conn.nodes()) {
        if (node && node->id() == id)
            return node;
    }
    return nullptr;
}

/// Resolve a target spec to a PwNode. Accepted forms:
/// - numeric id (e.g. `55`): exact id match
/// - `default.audio.sink` / `default.audio.source`: the WirePlumber
///   default at command time
/// - a `node.name` string (e.g. `alsa_output.pci-0000_00_1f.3.iec958-stereo`):
///   exact name match
///
/// Diagnostics are written to stderr on the sentinel-no-default path so
/// callers can distinguish "default sentinel hit an empty default" from
/// "spec didn't match anything". Returns nullptr on miss.
PhosphorServicePipeWire::PwNode* resolveTarget(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec)
{
    bool isNumeric = false;
    const uint id = spec.toUInt(&isNumeric);
    if (isNumeric)
        return findNode(conn, id);
    QString name = spec;
    if (spec == QLatin1String("default.audio.sink")) {
        name = conn.defaultSinkName();
        if (name.isEmpty()) {
            err() << "no default sink reported by WirePlumber\n";
            return nullptr;
        }
    } else if (spec == QLatin1String("default.audio.source")) {
        name = conn.defaultSourceName();
        if (name.isEmpty()) {
            err() << "no default source reported by WirePlumber\n";
            return nullptr;
        }
    }
    if (name.isEmpty())
        return nullptr;
    for (auto* node : conn.nodes()) {
        if (node && node->name() == name)
            return node;
    }
    return nullptr;
}

QString labelFor(PhosphorServicePipeWire::PwNode* node)
{
    if (!node)
        return {};
    const QString nick = node->nick();
    if (!nick.isEmpty())
        return nick;
    const QString desc = node->description();
    if (!desc.isEmpty())
        return desc;
    return node->name();
}

void printNode(PhosphorServicePipeWire::PwNode* node)
{
    if (!node)
        return;
    out() << "  " << node->id() << "  " << labelFor(node) << "\n"
          << "      name:        " << node->name() << "\n"
          << "      mediaClass:  " << node->mediaClass() << "\n"
          << "      channels:    " << node->channelCount() << "\n";
    const auto vols = node->volumes();
    if (!vols.isEmpty()) {
        out() << "      volumes:    [";
        for (int i = 0; i < vols.size(); ++i) {
            if (i > 0)
                out() << ", ";
            out() << QString::number(vols.at(i), 'f', 3);
        }
        out() << "]\n";
    }
    out() << "      muted:       " << (node->muted() ? "yes" : "no") << "\n";
}

int cmdList(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& kind)
{
    QSet<QString> wanted;
    if (kind == QLatin1String("sinks")) {
        wanted = {QStringLiteral("Audio/Sink")};
    } else if (kind == QLatin1String("sources")) {
        wanted = {QStringLiteral("Audio/Source")};
    } else if (kind == QLatin1String("streams")) {
        wanted = {QStringLiteral("Stream/Output/Audio"), QStringLiteral("Stream/Input/Audio")};
    } else {
        err() << "unknown list kind: " << kind << " (use sinks, sources, or streams)\n";
        return 1;
    }
    int count = 0;
    for (auto* node : conn.nodes()) {
        if (node && wanted.contains(node->mediaClass())) {
            printNode(node);
            ++count;
        }
    }
    if (count == 0)
        out() << "(no nodes match)\n";
    return 0;
}

int cmdDefault(PhosphorServicePipeWire::PipeWireConnection& conn)
{
    const QString sink = conn.defaultSinkName();
    const QString source = conn.defaultSourceName();
    out() << "default.audio.sink:    " << (sink.isEmpty() ? QStringLiteral("(none)") : sink) << "\n";
    out() << "default.audio.source:  " << (source.isEmpty() ? QStringLiteral("(none)") : source) << "\n";
    return 0;
}

int cmdSetVolume(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& target, double pct)
{
    if (!std::isfinite(pct) || pct < 0.0 || pct > 100.0) {
        err() << "set-volume pct must be a finite value in [0, 100], got " << pct << "\n";
        return 1;
    }
    auto* node = resolveTarget(conn, target);
    if (!node) {
        err() << "no node matches target '" << target << "'\n";
        return 1;
    }
    // Map 0..100 to linear amplitude 0.0..1.0. PipeWire's
    // SPA_PROP_channelVolumes domain is bounded at 1.0 (full scale).
    // Cubic / perceptual conversion is a UI concern (curves live in a
    // higher layer); the CLI takes a raw linear percentage so the call
    // is unambiguous.
    const qreal linear = pct / 100.0;
    node->setVolume(linear);
    out() << "set node " << node->id() << " (" << node->name() << ") volume = " << QString::number(linear, 'f', 3)
          << " (linear)\n";
    // The write is async; settle briefly so a subsequent read sees the
    // echoed pod.
    settleRegistry(kDefaultPostWriteSettleMs);
    return 0;
}

int cmdMute(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& target, bool muted)
{
    auto* node = resolveTarget(conn, target);
    if (!node) {
        err() << "no node matches target '" << target << "'\n";
        return 1;
    }
    node->setMuted(muted);
    out() << "set node " << node->id() << " (" << node->name() << ") muted = " << (muted ? "true" : "false") << "\n";
    settleRegistry(kDefaultPostWriteSettleMs);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
        return usage();
    const QString cmd = args.at(1);

    // Reject unknown commands BEFORE paying the daemon-connect cost.
    // A typo'd subcommand previously waited 2s for the handshake, then
    // returned usage().
    static const QStringList knownCommands = {QStringLiteral("list"),
                                              QStringLiteral("default"),
                                              QStringLiteral("set-volume"),
                                              QStringLiteral("mute"),
                                              QStringLiteral("unmute"),
                                              QStringLiteral("set-default-sink"),
                                              QStringLiteral("set-default-source")};
    if (!knownCommands.contains(cmd))
        return usage();

    PhosphorServicePipeWire::PipeWireConnection conn;
    const int timeoutMs = connectTimeoutMs();
    conn.connect();
    // waitForConnect re-checks isConnected() before exec()'ing its
    // QEventLoop, so a daemon that synchronously flips us into the
    // connected state between conn.connect() and the QObject::connect
    // installation is handled by the explicit early-out at the top of
    // waitForConnect, not by ordering. (Async handshakes via
    // pw_loop_invoke / queued signals can't complete synchronously
    // anyway, so this is belt-and-suspenders.)
    if (!waitForConnect(conn, timeoutMs)) {
        err() << "failed to connect to PipeWire daemon within " << timeoutMs << "ms\n";
        return 2;
    }
    settleRegistry();

    if (cmd == QLatin1String("list")) {
        if (args.size() < 3)
            return usage();
        return cmdList(conn, args.at(2));
    }
    if (cmd == QLatin1String("default"))
        return cmdDefault(conn);
    if (cmd == QLatin1String("set-volume")) {
        if (args.size() < 4)
            return usage();
        bool okPct = false;
        const double pct = args.at(3).toDouble(&okPct);
        if (!okPct)
            return usage();
        return cmdSetVolume(conn, args.at(2), pct);
    }
    if (cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) {
        if (args.size() < 3)
            return usage();
        return cmdMute(conn, args.at(2), cmd == QLatin1String("mute"));
    }
    if (cmd == QLatin1String("set-default-sink")) {
        if (args.size() < 3)
            return usage();
        // Trim BEFORE the sentinel check so trailing whitespace
        // ("default.audio.sink ") doesn't bypass the rejection and
        // write a corrupt name into WirePlumber's metadata.
        const QString name = args.at(2).trimmed();
        // The `default.audio.*` sentinels are read-side only; the
        // configured-default key needs a real node.name to do anything
        // meaningful. Reject up-front so callers don't silently write
        // the sentinel literally into WirePlumber's metadata.
        if (name.isEmpty() || name == QLatin1String("default.audio.sink")
            || name == QLatin1String("default.audio.source")) {
            err() << "set-default-sink requires a real node.name (see `list sinks`)\n";
            return 1;
        }
        conn.setDefaultSink(name);
        out() << "set default sink = " << name << "\n";
        settleRegistry(kDefaultPostMetadataSettleMs);
        return 0;
    }
    if (cmd == QLatin1String("set-default-source")) {
        if (args.size() < 3)
            return usage();
        const QString name = args.at(2).trimmed();
        if (name.isEmpty() || name == QLatin1String("default.audio.sink")
            || name == QLatin1String("default.audio.source")) {
            err() << "set-default-source requires a real node.name (see `list sources`)\n";
            return 1;
        }
        conn.setDefaultSource(name);
        out() << "set default source = " << name << "\n";
        settleRegistry(kDefaultPostMetadataSettleMs);
        return 0;
    }
    return usage();
}

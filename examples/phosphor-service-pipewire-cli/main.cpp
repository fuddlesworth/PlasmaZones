// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {

// Settle timeouts (ms). Tuned for a developer host with a local
// PipeWire daemon: handshake + initial registry walk completes well
// under 250 ms; param echoes after a write land within ~80 ms; metadata
// writes round-trip through WirePlumber within ~150 ms. The metadata
// spin-wait deadline is set generously because the round-trip varies
// with WirePlumber load. Override the connect timeout via
// PHOSPHOR_PW_CONNECT_TIMEOUT_MS for slow-boot scenarios.
constexpr int kDefaultPostReadSettleMs = 80;
constexpr int kDefaultPostWriteSettleMs = 200;
constexpr int kDefaultPostMetadataSettleMs = 500;
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

// Exit code convention:
//   0   success
//   1   runtime error (no matching node, write failed, list-kind invalid
//       post-connect, set-volume out of range, metadata write timed out
//       waiting for echo, param echo did not match the requested value)
//   2   connect timeout (daemon unreachable within PHOSPHOR_PW_CONNECT_TIMEOUT_MS)
//   64  usage error (bad/missing subcommand or arguments). Matches BSD
//       sysexits.h EX_USAGE so wrapper scripts can distinguish "you
//       typed the command wrong" from "the system is broken".
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
          << "exit codes:\n"
          << "  0   success\n"
          << "  1   runtime error (unknown target, mismatched echo, etc.)\n"
          << "  2   connect timeout (daemon unreachable)\n"
          << "  64  usage error (bad/missing arguments)\n"
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
/// time out. Early-outs if `isConnected()` is already true; otherwise
/// blocks on a local QEventLoop driven by the slot-driven async
/// handshake. The CLI is a one-shot tool: the daemon either answers
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
    // Reject any input containing internal whitespace: " 55" would
    // otherwise toUInt() to 55 silently, and "alsa output" is never a
    // valid node.name anyway. We accept leading/trailing whitespace by
    // trimming first.
    const QString trimmed = spec.trimmed();
    if (trimmed.contains(QChar::Space) || trimmed.contains(QLatin1Char('\t'))) {
        err() << "target '" << spec << "' contains whitespace\n";
        return nullptr;
    }
    bool isNumeric = false;
    const uint id = trimmed.toUInt(&isNumeric);
    if (isNumeric)
        return findNode(conn, id);
    QString name = trimmed;
    if (trimmed == QLatin1String("default.audio.sink")) {
        name = conn.defaultSinkName();
        if (name.isEmpty()) {
            err() << "no node matches default sentinel (defaultSinkName empty)\n";
            return nullptr;
        }
    } else if (trimmed == QLatin1String("default.audio.source")) {
        name = conn.defaultSourceName();
        if (name.isEmpty()) {
            err() << "no node matches default sentinel (defaultSourceName empty)\n";
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

// Known list kinds. Used both for the pre-connect validation in main()
// (so a typo'd kind fails fast) and inside cmdList() for the actual
// filter. Kept in lock-step with the audio media-class strings.
//
// Note: the audio media-class strings ("Audio/Sink", "Audio/Source",
// "Stream/Output/Audio", "Stream/Input/Audio") are duplicated here, in
// the smoke tests, and inside PwNodeModel's filtering. Consolidating
// them behind a single public header constant set is a separate change
// that touches the library API; this CLI keeps the literals inline so
// it remains a stand-alone consumer of the public types.
bool isKnownListKind(const QString& kind)
{
    return kind == QLatin1String("sinks") || kind == QLatin1String("sources") || kind == QLatin1String("streams");
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
        // Already filtered out in main() before connect; keep this as a
        // belt-and-suspenders guard so the function is safe to call
        // standalone.
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

/// Wait for a `propsChanged` echo from `node` and verify the predicate
/// holds. Returns true once the predicate is satisfied within
/// `timeoutMs`, false on timeout. Unrelated emissions (a parallel app
/// tweaking volume, a periodic heartbeat) only cause the loop to
/// re-check the predicate; the wait continues until the predicate
/// matches or the deadline elapses. `label` is used for the timeout
/// diagnostic on stderr.
bool waitForPropsEchoAndVerify(PhosphorServicePipeWire::PwNode* node, const std::function<bool()>& predicate,
                               int timeoutMs, const char* label)
{
    if (!node)
        return false;
    if (predicate())
        return true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        auto conn1 = QObject::connect(node, &PhosphorServicePipeWire::PwNode::propsChanged, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(conn1);
        if (predicate())
            return true;
        // Unrelated emission (or spurious wake): fall through and
        // re-arm the wait for any remaining budget.
    }
    err() << label << ": timed out waiting for propsChanged echo (" << timeoutMs << "ms)\n";
    return false;
}

int cmdSetVolume(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& target, double pct)
{
    if (!std::isfinite(pct) || pct < 0.0 || pct > 100.0) {
        // Range failures route through rc=1 (not rc=64) because they are
        // detected after the daemon has been contacted; the usage error
        // would be misleading once we've already paid the connect cost.
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
    // Verify the daemon echoed our new pod. Use a tolerant epsilon
    // because the daemon may quantise the linear amplitude on its way
    // through SPA_PARAM_Props. waitForPropsEchoAndVerify re-arms on
    // unrelated emissions so a parallel app tweaking another channel
    // can't cause an early-quit.
    constexpr qreal kVolumeEpsilon = 0.01;
    const auto matchesRequest = [node, linear]() {
        const auto echoed = node->volumes();
        if (echoed.isEmpty())
            return false;
        for (const qreal v : echoed) {
            if (std::fabs(v - linear) > kVolumeEpsilon)
                return false;
        }
        return true;
    };
    node->setVolume(linear);
    const bool echoed = waitForPropsEchoAndVerify(node, matchesRequest, kDefaultPostWriteSettleMs, "set-volume");
    const auto echoedVolumes = node->volumes();
    out() << "set node " << node->id() << " (" << node->name() << ") volume = " << QString::number(linear, 'f', 3)
          << " (linear), echoed = [";
    for (int i = 0; i < echoedVolumes.size(); ++i) {
        if (i > 0)
            out() << ", ";
        out() << QString::number(echoedVolumes.at(i), 'f', 3);
    }
    out() << "]\n";
    if (!echoed) {
        // Distinguish a true timeout (no matching echo) from a mismatch
        // (echo arrived but values diverged). Both route through rc=1
        // because the contract is the same: we couldn't confirm the
        // requested value landed.
        for (const qreal v : echoedVolumes) {
            if (std::fabs(v - linear) > kVolumeEpsilon) {
                err() << "set-volume: echoed value " << QString::number(v, 'f', 3) << " differs from requested "
                      << QString::number(linear, 'f', 3) << "\n";
                return 1;
            }
        }
        return 1;
    }
    return 0;
}

int cmdMute(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& target, bool muted)
{
    auto* node = resolveTarget(conn, target);
    if (!node) {
        err() << "no node matches target '" << target << "'\n";
        return 1;
    }
    // Hook the echo wait BEFORE issuing the write so we can confirm
    // PipeWire actually committed the new mute state; otherwise the
    // CLI returns success even when the write was silently dropped.
    const auto matchesRequest = [node, muted]() {
        return node->muted() == muted;
    };
    node->setMuted(muted);
    const bool echoed = waitForPropsEchoAndVerify(node, matchesRequest, kDefaultPostWriteSettleMs, "mute");
    out() << "set node " << node->id() << " (" << node->name() << ") muted = " << (muted ? "true" : "false")
          << ", echoed = " << (node->muted() ? "true" : "false") << "\n";
    if (!echoed) {
        if (node->muted() != muted) {
            err() << "mute: echoed value " << (node->muted() ? "true" : "false") << " differs from requested "
                  << (muted ? "true" : "false") << "\n";
        }
        return 1;
    }
    return 0;
}

/// Signal-driven wait until `accessor()` equals `expected` or the
/// deadline elapses. Used for set-default-sink / set-default-source so
/// the CLI returns success only when WirePlumber has actually committed
/// the new default. `connectSignal` wires the change signal to the
/// loop's quit slot; unrelated emissions (the metadata observer firing
/// for a different key) just cause a re-check of the predicate, so we
/// keep waiting until either the value matches or the deadline elapses.
/// Returns true on match, false on timeout. Parallel to
/// waitForConnect's shape.
bool waitForString(PhosphorServicePipeWire::PipeWireConnection& conn,
                   void (PhosphorServicePipeWire::PipeWireConnection::*signal)(),
                   const std::function<QString()>& accessor, const QString& expected, int timeoutMs)
{
    if (accessor() == expected)
        return true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        auto conn1 = QObject::connect(&conn, signal, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(conn1);
        if (accessor() == expected)
            return true;
        // Unrelated emission: fall through and re-arm for any remaining
        // budget.
    }
    return accessor() == expected;
}

/// Resolve a target spec to a node.name string, accepting numeric ids
/// by pre-resolving via findNode/resolveTarget. Without this, a user
/// typing `set-default-sink 55` would write the literal string "55" into
/// WirePlumber's default-metadata key. On miss, returns an empty string
/// and writes an error to stderr.
QString resolveTargetToName(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec)
{
    auto* node = resolveTarget(conn, spec);
    if (!node) {
        err() << "no node matches target '" << spec << "'\n";
        return {};
    }
    return node->name();
}

/// Explicit disconnect + a brief settle so the loop thread has time to
/// flush any in-flight write before the connection destructor runs. The
/// destructor handles teardown safely on its own, but routing through
/// disconnect() first makes the teardown order deterministic for the
/// metadata/volume write paths the CLI exercises.
void cleanShutdown(PhosphorServicePipeWire::PipeWireConnection& conn)
{
    conn.disconnect();
    QEventLoop loop;
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
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

    // Validate `list <kind>` before connecting too: a bad kind used to
    // pay the full 2s handshake cost only to return 1, which is
    // indistinguishable from a real runtime error from the user's
    // perspective.
    if (cmd == QLatin1String("list")) {
        if (args.size() < 3)
            return usage();
        if (!isKnownListKind(args.at(2))) {
            err() << "unknown list kind: " << args.at(2) << " (use sinks, sources, or streams)\n";
            return usage();
        }
    }
    // Same rationale for the write subcommands: a bare `mute` or
    // `set-volume foo` previously paid the full 2s handshake before
    // discovering it was a usage error. Hoist the arg-count checks here
    // so under-arg typos fail fast with rc=64.
    if (cmd == QLatin1String("set-volume") && args.size() < 4)
        return usage();
    if ((cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) && args.size() < 3)
        return usage();
    if ((cmd == QLatin1String("set-default-sink") || cmd == QLatin1String("set-default-source")) && args.size() < 3)
        return usage();

    PhosphorServicePipeWire::PipeWireConnection conn;
    const int timeoutMs = connectTimeoutMs();
    conn.connect();
    // waitForConnect early-outs if the connection is already connected,
    // otherwise it blocks on a local QEventLoop driven by the
    // connectedChanged signal. Async handshakes via pw_loop_invoke /
    // queued signals can't complete synchronously, so the slot-driven
    // path is always what runs.
    if (!waitForConnect(conn, timeoutMs)) {
        err() << "failed to connect to PipeWire daemon within " << timeoutMs << "ms\n";
        cleanShutdown(conn);
        return 2;
    }
    settleRegistry();

    int rc = 0;
    if (cmd == QLatin1String("list")) {
        rc = cmdList(conn, args.at(2));
    } else if (cmd == QLatin1String("default")) {
        rc = cmdDefault(conn);
    } else if (cmd == QLatin1String("set-volume")) {
        // Arg-count was validated pre-connect; only the parse can fail
        // here. A non-numeric pct is a usage error, not a runtime one.
        bool okPct = false;
        const double pct = args.at(3).toDouble(&okPct);
        if (!okPct) {
            cleanShutdown(conn);
            return usage();
        }
        rc = cmdSetVolume(conn, args.at(2), pct);
    } else if (cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) {
        rc = cmdMute(conn, args.at(2), cmd == QLatin1String("mute"));
    } else if (cmd == QLatin1String("set-default-sink")) {
        // Trim BEFORE the sentinel check so trailing whitespace
        // ("default.audio.sink ") doesn't bypass the rejection and
        // write a corrupt name into WirePlumber's metadata.
        const QString spec = args.at(2).trimmed();
        // The `default.audio.*` sentinels are read-side only; the
        // configured-default key needs a real node.name to do anything
        // meaningful. Reject up-front so callers don't silently write
        // the sentinel literally into WirePlumber's metadata.
        if (spec.isEmpty() || spec == QLatin1String("default.audio.sink")
            || spec == QLatin1String("default.audio.source")) {
            err() << "set-default-sink requires a real node.name (see `list sinks`)\n";
            rc = 1;
        } else {
            // Pre-resolve so a numeric id is converted to the matching
            // node.name; writing "55" verbatim into WirePlumber's
            // default metadata would corrupt the key for every other
            // session-manager consumer.
            const QString name = resolveTargetToName(conn, spec);
            if (name.isEmpty()) {
                rc = 1;
            } else {
                conn.setDefaultSink(name);
                // Signal-driven wait until WirePlumber echoes the new
                // default back through the metadata observer. On
                // timeout exit non-zero so wrapper scripts can detect
                // partial application.
                if (!waitForString(
                        conn, &PhosphorServicePipeWire::PipeWireConnection::defaultSinkNameChanged,
                        [&conn]() {
                            return conn.defaultSinkName();
                        },
                        name, kDefaultPostMetadataSettleMs)) {
                    err() << "set-default-sink: timed out waiting for WirePlumber echo ("
                          << kDefaultPostMetadataSettleMs << "ms)\n";
                    rc = 1;
                } else {
                    out() << "set default sink = " << name << "\n";
                }
            }
        }
    } else if (cmd == QLatin1String("set-default-source")) {
        const QString spec = args.at(2).trimmed();
        if (spec.isEmpty() || spec == QLatin1String("default.audio.sink")
            || spec == QLatin1String("default.audio.source")) {
            err() << "set-default-source requires a real node.name (see `list sources`)\n";
            rc = 1;
        } else {
            const QString name = resolveTargetToName(conn, spec);
            if (name.isEmpty()) {
                rc = 1;
            } else {
                conn.setDefaultSource(name);
                if (!waitForString(
                        conn, &PhosphorServicePipeWire::PipeWireConnection::defaultSourceNameChanged,
                        [&conn]() {
                            return conn.defaultSourceName();
                        },
                        name, kDefaultPostMetadataSettleMs)) {
                    err() << "set-default-source: timed out waiting for WirePlumber echo ("
                          << kDefaultPostMetadataSettleMs << "ms)\n";
                    rc = 1;
                } else {
                    out() << "set default source = " << name << "\n";
                }
            }
        }
    } else {
        // Unreachable: the pre-connect knownCommands guard already
        // routed every unknown subcommand through usage(). Keep this
        // arm as a hard-fail so a future command added to
        // knownCommands but not wired up here surfaces as a logic
        // bug instead of silently exiting 0.
        Q_UNREACHABLE();
    }
    cleanShutdown(conn);
    return rc;
}

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QPointer>
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
// with WirePlumber load. Slow-boot or loaded CI machines can override
// the connect timeout via PHOSPHOR_PW_CONNECT_TIMEOUT_MS, the
// post-write (volume/mute echo) deadline via
// PHOSPHOR_PW_POST_WRITE_SETTLE_MS, and the post-metadata
// (set-default-sink/source echo) deadline via
// PHOSPHOR_PW_POST_METADATA_SETTLE_MS.
constexpr int kDefaultPostReadSettleMs = 80; // no env override: settle hint only, not an echo deadline
constexpr int kDefaultPostWriteSettleMs = 200;
constexpr int kDefaultPostMetadataSettleMs = 500;
constexpr int kDefaultConnectTimeoutMs = 2000;
// Volume echo tolerance: the daemon may quantise the linear amplitude
// on its way through SPA_PARAM_Props, so set-volume's predicate uses a
// non-zero epsilon. Overridable via PHOSPHOR_PW_VOLUME_ECHO_EPSILON for
// hosts whose daemon quantises more aggressively.
constexpr qreal kDefaultVolumeEchoEpsilon = 0.01;
// Shutdown settle: small fixed budget for the loop thread to flush any
// in-flight write before ~PipeWireConnection runs. Not env-overridable
// because the destructor handles teardown safely on its own; this is
// just an ordering hint for the metadata/volume write paths.
constexpr int kDefaultShutdownSettleMs = 50;

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
//   70  internal software error (e.g., maintainer-introduced dispatch gap
//       where a command was added to knownCommands without a matching
//       dispatch arm). Matches BSD sysexits.h EX_SOFTWARE.
//
// Always returns 64 (EX_USAGE) so callers can `return usage();` from
// any error-detection arm without re-stating the exit code at every
// call site.
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
          << "  set-default-sink <target>           write default.configured.audio.sink\n"
          << "  set-default-source <target>         write default.configured.audio.source\n"
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
          << "  70  internal software error (e.g., maintainer-introduced dispatch gap)\n"
          << "\n"
          << "env overrides (malformed values fall back to defaults):\n"
          << "  PHOSPHOR_PW_CONNECT_TIMEOUT_MS         daemon-connect timeout       (positive int ms, default 2000)\n"
          << "  PHOSPHOR_PW_POST_WRITE_SETTLE_MS       volume/mute echo deadline    (positive int ms, default 200)\n"
          << "  PHOSPHOR_PW_POST_METADATA_SETTLE_MS    set-default echo deadline    (positive int ms, default 500)\n"
          << "  PHOSPHOR_PW_VOLUME_ECHO_EPSILON        set-volume echo tolerance    (positive double, default 0.01)\n";
    return 64; // EX_USAGE
}

/// Read a positive-int env var override, or fall back to `fallbackMs`
/// on empty / malformed / non-positive values. Centralises the parse so
/// every settle-timeout knob has the same defensive behaviour: a typo'd
/// env value never silently routes through to 0 (which would defeat the
/// echo waits entirely).
int envOverrideMs(const char* name, int fallbackMs)
{
    const QByteArray overrideEnv = qgetenv(name);
    if (overrideEnv.isEmpty())
        return fallbackMs;
    bool ok = false;
    const int v = overrideEnv.toInt(&ok);
    return (ok && v > 0) ? v : fallbackMs;
}

/// Parallel to envOverrideMs for positive double-valued env vars (e.g.
/// the volume-echo tolerance epsilon). Empty / malformed / non-finite /
/// non-positive values fall back to `fallback`; a typo'd env value
/// never silently routes through to 0 (which would defeat the echo
/// check entirely).
qreal envOverrideDouble(const char* name, qreal fallback)
{
    const QByteArray overrideEnv = qgetenv(name);
    if (overrideEnv.isEmpty())
        return fallback;
    bool ok = false;
    const double v = QString::fromUtf8(overrideEnv).toDouble(&ok);
    return (ok && std::isfinite(v) && v > 0.0) ? v : fallback;
}

/// Connect-timeout sourced from the env var so users hitting slow-boot
/// scenarios can override without recompiling.
int connectTimeoutMs()
{
    return envOverrideMs("PHOSPHOR_PW_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
}

/// Volume-echo tolerance: daemons may quantise linear amplitude on
/// SPA_PARAM_Props, so the set-volume predicate uses a non-zero epsilon.
/// Overridable via PHOSPHOR_PW_VOLUME_ECHO_EPSILON for hosts whose
/// daemon quantises more aggressively.
qreal volumeEchoEpsilon()
{
    return envOverrideDouble("PHOSPHOR_PW_VOLUME_ECHO_EPSILON", kDefaultVolumeEchoEpsilon);
}

/// Post-write (volume/mute) settle deadline. Loaded CI hosts can extend
/// the window via PHOSPHOR_PW_POST_WRITE_SETTLE_MS without recompiling.
int postWriteSettleMs()
{
    return envOverrideMs("PHOSPHOR_PW_POST_WRITE_SETTLE_MS", kDefaultPostWriteSettleMs);
}

/// Post-metadata (set-default-sink/source) settle deadline. WirePlumber
/// round-trip varies with load; extend via
/// PHOSPHOR_PW_POST_METADATA_SETTLE_MS on slow hosts.
int postMetadataSettleMs()
{
    return envOverrideMs("PHOSPHOR_PW_POST_METADATA_SETTLE_MS", kDefaultPostMetadataSettleMs);
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
    // trimming first. Iterating QChar walks UTF-16 code units, so
    // per-char QChar::isSpace() covers the full BMP Unicode whitespace
    // set (NBSP, U+2028 line separator, etc.); supplementary-plane
    // whitespace codepoints (none currently assigned in Unicode) would
    // not be detected, but a narrow space/tab-only check would silently
    // let the BMP cases through.
    const QString trimmed = spec.trimmed();
    for (const QChar ch : trimmed) {
        if (ch.isSpace()) {
            err() << "target '" << spec << "' contains whitespace\n";
            return nullptr;
        }
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
/// `timeoutMs`, false on timeout or mid-wait removal. Unrelated
/// emissions (a parallel app tweaking volume, a periodic heartbeat)
/// only cause the loop to re-check the predicate; the wait continues
/// until the predicate matches or the deadline elapses. `label` is used
/// for the failure diagnostic on stderr.
///
/// The `node` handle is guarded by QPointer so a daemon-side node
/// removal mid-write cleanly breaks the loop instead of leaving the
/// lambda calling methods on a dangling pointer. Callers MUST also
/// guard any references they capture in their predicate (otherwise the
/// predicate itself dereferences a dangling pointer); the canonical
/// pattern is to copy a `QPointer<PwNode>` into the predicate capture
/// and null-check it before dereferencing.
///
/// After the deadline elapses we run the predicate one final time so a
/// late echo that landed during the disconnect / loop-exit window is
/// still caught. This mirrors waitForString's post-deadline
/// `accessor() == expected` final read (the guard handling differs:
/// waitForString has no guarded receiver).
///
/// On failure the diagnostic distinguishes "node removed mid-wait"
/// (guard null at deadline) from a plain timeout — the two cases call
/// for different operator action (re-list the registry vs. raise the
/// echo budget), and a unified "timed out" message obscured that.
bool waitForPropsEchoAndVerify(PhosphorServicePipeWire::PwNode* node, const std::function<bool()>& predicate,
                               int timeoutMs, const char* label)
{
    QPointer<PhosphorServicePipeWire::PwNode> guard(node);
    if (!guard) {
        err() << label << ": node removed before wait started\n";
        return false;
    }
    if (predicate())
        return true;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!guard)
            break;
        QEventLoop loop;
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0)
            break;
        // Receiver is &loop (not guard.data()) so the connection auto-disconnects
        // when the loop is destroyed at scope exit; Qt no-ops a disconnect on a
        // dead sender, so a daemon-side node removal mid-wait is also safe.
        auto qtConn = QObject::connect(guard.data(), &PhosphorServicePipeWire::PwNode::propsChanged, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(qtConn);
        if (!guard)
            break;
        if (predicate())
            return true;
        // Unrelated emission (or spurious wake): fall through and
        // re-arm the wait for any remaining budget.
    }
    // Final recheck: a propsChanged that arrived during the disconnect
    // / loop-exit window would otherwise be missed, producing a spurious
    // timeout even though the daemon committed the value.
    if (guard && predicate())
        return true;
    if (!guard)
        err() << label << ": node removed during wait\n";
    else
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
    // Snapshot id + name BEFORE issuing the write. id is a CONSTANT
    // property on PwNode and name is effectively constant for the
    // lifetime of the node, but the node itself can be removed by the
    // daemon mid-wait — reading either via the raw pointer after the
    // wait would be UB. The snapshots let us emit complete diagnostics
    // (including which node we tried to write) even if the node went
    // away while we were waiting for the echo.
    const uint nodeId = node->id();
    const QString nodeName = node->name();
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
    // can't cause an early-quit. Epsilon is env-overridable via
    // PHOSPHOR_PW_VOLUME_ECHO_EPSILON for hosts with aggressive
    // quantisation.
    const qreal volumeEpsilon = volumeEchoEpsilon();
    // Capture the matched snapshot inside the predicate so a later
    // unrelated propsChanged can't clobber the values we print. Without
    // this, `node->volumes()` re-read after the wait could show a
    // divergent snapshot (e.g. another channel updated between match
    // and read) that doesn't reflect what we actually saw match.
    //
    // QPointer-guard the node capture so a daemon-side removal mid-wait
    // doesn't leave the predicate dereferencing a dangling pointer —
    // waitForPropsEchoAndVerify guards its own internal receiver
    // similarly but cannot reach into the caller's capture list.
    QPointer<PhosphorServicePipeWire::PwNode> nodePtr(node);
    QList<qreal> matchedVolumes;
    const auto matchesRequest = [nodePtr, linear, volumeEpsilon, &matchedVolumes]() {
        if (!nodePtr)
            return false;
        const auto echoed = nodePtr->volumes();
        if (echoed.isEmpty())
            return false;
        for (const qreal v : echoed) {
            if (std::fabs(v - linear) > volumeEpsilon)
                return false;
        }
        matchedVolumes = echoed;
        return true;
    };
    node->setVolume(linear);
    const bool echoed = waitForPropsEchoAndVerify(node, matchesRequest, postWriteSettleMs(), "set-volume");
    // On match, print the captured matched snapshot. On timeout AND
    // the node is still alive, fall back to the live volumes so the
    // diagnostic shows what the daemon currently exposes. On timeout
    // AND the node has been removed mid-wait, reading volumes() would
    // be UB — leave the echoed list empty and downstream printing will
    // surface an empty `[]` alongside the "node removed during wait"
    // diagnostic that waitForPropsEchoAndVerify already emitted.
    QList<qreal> echoedVolumes;
    if (echoed)
        echoedVolumes = matchedVolumes;
    else if (nodePtr)
        echoedVolumes = nodePtr->volumes();
    out() << "set node " << nodeId << " (" << nodeName << ") volume = " << QString::number(linear, 'f', 3)
          << " (linear), echoed = [";
    for (int i = 0; i < echoedVolumes.size(); ++i) {
        if (i > 0)
            out() << ", ";
        out() << QString::number(echoedVolumes.at(i), 'f', 3);
    }
    out() << "]\n";
    // On timeout, waitForPropsEchoAndVerify already logged the timeout
    // diagnostic; we just route through rc=1 to surface the failure.
    if (!echoed)
        return 1;
    return 0;
}

int cmdMute(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& target, bool muted)
{
    auto* node = resolveTarget(conn, target);
    if (!node) {
        err() << "no node matches target '" << target << "'\n";
        return 1;
    }
    // Snapshot id + name BEFORE issuing the write. id is a CONSTANT
    // property on PwNode and name is effectively constant for the
    // lifetime of the node, but the node itself can be removed by the
    // daemon mid-wait — reading either via the raw pointer after the
    // wait would be UB. The snapshots let us emit complete diagnostics
    // (including which node we tried to write) even if the node went
    // away while we were waiting for the echo.
    const uint nodeId = node->id();
    const QString nodeName = node->name();
    // Hook the echo wait BEFORE issuing the write so we can confirm
    // PipeWire actually committed the new mute state; otherwise the
    // CLI returns success even when the write was silently dropped.
    //
    // The predicate narrows to muted == requested, so on success the
    // observed mute state is by definition equal to the requested
    // value. No need to capture a separate snapshot.
    //
    // QPointer-guard the node capture so a daemon-side removal mid-wait
    // doesn't leave the predicate dereferencing a dangling pointer —
    // waitForPropsEchoAndVerify guards its own internal receiver
    // similarly but cannot reach into the caller's capture list.
    QPointer<PhosphorServicePipeWire::PwNode> nodePtr(node);
    const auto matchesRequest = [nodePtr, muted]() {
        if (!nodePtr)
            return false;
        return nodePtr->muted() == muted;
    };
    node->setMuted(muted);
    // Shares postWriteSettleMs() with set-volume: both ride the same
    // SPA_PARAM_Props echo path so a single tunable governs both
    // deadlines. The predicate narrows to muted == requested: a
    // parallel session manager committing the OPPOSITE state only
    // delays the match; one committing the requested state satisfies
    // the predicate, which is acceptable because the observable
    // end-state is what we contracted for.
    const bool echoed = waitForPropsEchoAndVerify(node, matchesRequest, postWriteSettleMs(), "mute");
    // On match, the predicate guarantees observed == requested. On
    // timeout AND the node is still alive, read the live mute state so
    // the diagnostic shows what the daemon currently exposes. On
    // timeout AND the node has been removed mid-wait, reading muted()
    // would be UB — surface the requested value (the same value that
    // wouldn't echo) so the diagnostic stays self-consistent alongside
    // the "node removed during wait" message that
    // waitForPropsEchoAndVerify already emitted.
    bool echoedMuted;
    if (echoed)
        echoedMuted = muted;
    else if (nodePtr)
        echoedMuted = nodePtr->muted();
    else
        echoedMuted = muted;
    out() << "set node " << nodeId << " (" << nodeName << ") muted = " << (muted ? "true" : "false")
          << ", echoed = " << (echoedMuted ? "true" : "false") << "\n";
    // On timeout, waitForPropsEchoAndVerify already logged the timeout
    // diagnostic; we just route through rc=1 to surface the failure.
    if (!echoed)
        return 1;
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
        auto qtConn = QObject::connect(&conn, signal, &loop, [&loop]() {
            loop.quit();
        });
        QTimer::singleShot(remaining, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(qtConn);
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

/// Shared body for set-default-sink and set-default-source. The two
/// subcommands differ only in the signal they listen on, the accessor
/// they spin-wait against, the setter they invoke, and the diagnostic
/// label they print. Extracting the body keeps the dispatch arms thin
/// and stops a fix to one branch silently skipping the other.
///
/// Returns rc: 0 on echoed success, 1 on sentinel rejection / unresolved
/// target / timeout.
int cmdSetDefault(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec, const char* label,
                  void (PhosphorServicePipeWire::PipeWireConnection::*changedSignal)(),
                  const std::function<QString()>& accessor, const std::function<void(const QString&)>& setter)
{
    const QString trimmed = spec.trimmed();
    // The `default.audio.*` sentinels are read-side only; the
    // configured-default key needs a real node.name to do anything
    // meaningful. Reject up-front so callers don't silently write the
    // sentinel literally into WirePlumber's metadata. Distinguish the
    // two sentinels in the diagnostic so users typing the wrong one (or
    // hitting one via a shell alias) see which sentinel was rejected.
    if (trimmed.isEmpty()) {
        err() << label << " requires a real node.name (got empty spec)\n";
        return 1;
    }
    if (trimmed == QLatin1String("default.audio.sink")) {
        err() << label << " rejects 'default.audio.sink' sentinel; pass a real node.name (see `list sinks`)\n";
        return 1;
    }
    if (trimmed == QLatin1String("default.audio.source")) {
        err() << label << " rejects 'default.audio.source' sentinel; pass a real node.name (see `list sources`)\n";
        return 1;
    }
    // Pre-resolve so a numeric id is converted to the matching
    // node.name; writing "55" verbatim into WirePlumber's default
    // metadata would corrupt the key for every other session-manager
    // consumer.
    const QString name = resolveTargetToName(conn, trimmed);
    if (name.isEmpty())
        return 1;
    // Snapshot the accessor before issuing the write. If it stays empty
    // through the entire wait, the host almost certainly has no session
    // manager (PipeWire alone never publishes the default-sink/source
    // metadata key — WirePlumber does), which is structurally distinct
    // from "session manager present but slow". The two cases need
    // different operator responses, so we surface them separately rather
    // than collapsing into one timeout string.
    const QString preWriteValue = accessor();
    setter(name);
    // Signal-driven wait until WirePlumber echoes the new default back
    // through the metadata observer. On timeout exit non-zero so wrapper
    // scripts can detect partial application.
    const int deadlineMs = postMetadataSettleMs();
    if (!waitForString(conn, changedSignal, accessor, name, deadlineMs)) {
        if (preWriteValue.isEmpty() && accessor().isEmpty()) {
            // The metadata key was empty before the write and remains
            // empty after the full deadline. WirePlumber would have
            // populated it during the initial registry walk; its absence
            // points at a bare PipeWire daemon with no session manager
            // rather than a slow echo.
            err() << label << ": no WirePlumber default metadata observed for '" << name
                  << "'; PipeWire may be running without a session manager (" << deadlineMs << "ms)\n";
        } else {
            // Include the requested name in the diagnostic so operators can
            // tell which write was waiting: a parallel session manager
            // racing on the same default would otherwise produce a
            // diagnostic indistinguishable from a slow WirePlumber.
            err() << label << ": timed out waiting for WirePlumber echo of '" << name << "' (" << deadlineMs << "ms)\n";
        }
        return 1;
    }
    out() << "set " << label << " = " << name << "\n";
    return 0;
}

/// Explicit disconnect + a brief settle so the loop thread has time to
/// flush any in-flight write before the connection destructor runs. The
/// destructor handles teardown safely on its own, but routing through
/// disconnectFromDaemon() first makes the teardown order deterministic
/// for the metadata/volume write paths the CLI exercises.
///
/// When the connection never reached the connected state (the connect
/// timeout path), there is no in-flight write to flush, so the settle
/// window is pure overhead. Skip it in that case — the disconnect
/// itself is a documented no-op pre-connect, and tools that probe a
/// no-daemon host should return promptly rather than burning 50ms of
/// idle wait on every failed connect.
void cleanShutdown(PhosphorServicePipeWire::PipeWireConnection& conn)
{
    const bool wasConnected = conn.isConnected();
    conn.disconnectFromDaemon();
    if (!wasConnected)
        return;
    QEventLoop loop;
    QTimer::singleShot(kDefaultShutdownSettleMs, &loop, &QEventLoop::quit);
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
    // perspective. argv[0]=program, [1]=cmd, [2]=kind — strict
    // equality so trailing-junk ("list sinks oops") also fails fast.
    if (cmd == QLatin1String("list")) {
        if (args.size() != 3)
            return usage();
        if (!isKnownListKind(args.at(2))) {
            err() << "unknown list kind: " << args.at(2) << " (use sinks, sources, or streams)\n";
            return usage();
        }
    }
    // Same rationale for the write subcommands: a bare `mute` or
    // `set-volume foo` previously paid the full 2s handshake before
    // discovering it was a usage error. Hoist the arg-count checks here
    // so under-arg AND trailing-junk typos fail fast with rc=64.
    if (cmd == QLatin1String("set-volume") && args.size() != 4)
        return usage();
    if ((cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) && args.size() != 3)
        return usage();
    if ((cmd == QLatin1String("set-default-sink") || cmd == QLatin1String("set-default-source")) && args.size() != 3)
        return usage();
    // `default` takes no extra arguments; trailing junk is a usage error.
    if (cmd == QLatin1String("default") && args.size() != 2)
        return usage();

    PhosphorServicePipeWire::PipeWireConnection conn;
    const int timeoutMs = connectTimeoutMs();
    conn.connectToDaemon();
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
        // Surface the offending token before routing through usage() so
        // the user sees what went wrong instead of just the full help
        // wall — usage() alone makes a "set-volume sink abc" typo look
        // identical to "set-volume" with no args at all.
        bool okPct = false;
        const double pct = args.at(3).toDouble(&okPct);
        if (!okPct) {
            err() << "set-volume pct must be a number, got '" << args.at(3) << "'\n";
            cleanShutdown(conn);
            return usage();
        }
        rc = cmdSetVolume(conn, args.at(2), pct);
    } else if (cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) {
        rc = cmdMute(conn, args.at(2), cmd == QLatin1String("mute"));
    } else if (cmd == QLatin1String("set-default-sink")) {
        rc = cmdSetDefault(
            conn, args.at(2), "default sink", &PhosphorServicePipeWire::PipeWireConnection::defaultSinkNameChanged,
            [&conn]() {
                return conn.defaultSinkName();
            },
            [&conn](const QString& name) {
                conn.setDefaultSink(name);
            });
    } else if (cmd == QLatin1String("set-default-source")) {
        rc = cmdSetDefault(
            conn, args.at(2), "default source", &PhosphorServicePipeWire::PipeWireConnection::defaultSourceNameChanged,
            [&conn]() {
                return conn.defaultSourceName();
            },
            [&conn](const QString& name) {
                conn.setDefaultSource(name);
            });
    } else {
        // The pre-connect knownCommands guard already routes every
        // unknown subcommand through usage(), so reaching this arm
        // means a future maintainer added a command to knownCommands
        // without wiring up its dispatch. Q_UNREACHABLE() is only a
        // hint in release builds (UB), so emit a real diagnostic and
        // return a distinct exit code so the misbehaviour surfaces
        // observably instead of silently exiting 0.
        err() << "internal logic error: unhandled command " << cmd << " (rc=70, EX_SOFTWARE)\n";
        cleanShutdown(conn);
        return 70; // EX_SOFTWARE
    }
    cleanShutdown(conn);
    return rc;
}

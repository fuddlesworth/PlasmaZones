// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_env.h"
#include "cli_io.h"
#include "cli_target.h"
#include "cli_wait.h"

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <functional>

namespace {

using PhosphorPipeWireCli::err;
using PhosphorPipeWireCli::out;

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

int cmdList(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& kind)
{
    const QSet<QString> wanted = PhosphorPipeWireCli::kindToMediaClasses(kind);
    if (wanted.isEmpty()) {
        // Already filtered out in main() before connect; keep this as a
        // belt-and-suspenders guard so the function is safe to call
        // standalone.
        err() << "unknown list kind: " << kind << " (use sinks, sources, or streams)\n";
        return 1;
    }
    int count = 0;
    for (auto* node : conn.nodes()) {
        if (node && wanted.contains(node->mediaClass())) {
            PhosphorPipeWireCli::printNode(node);
            ++count;
        }
    }
    if (count == 0)
        out() << "(no nodes match)\n";
    // Flush at command exit so pipe-truncated consumers see the trailing
    // "(no nodes match)" line (or the last node's tail bytes) instead of
    // a buffered remainder discarded on pipe close. printNode flushes
    // per-node; this flush covers the trailing "(no nodes match)" line
    // and any QTextStream buffer state left after the loop.
    out().flush();
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
        // Range failures route through rc=1 (not rc=64) because they are
        // detected after the daemon has been contacted; the usage error
        // would be misleading once we've already paid the connect cost.
        err() << "set-volume pct must be a finite value in [0, 100], got " << pct << "\n";
        return 1;
    }
    auto* node = PhosphorPipeWireCli::resolveTarget(conn, target);
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
    const qreal volumeEpsilon = PhosphorPipeWireCli::volumeEchoEpsilon();
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
    const bool echoed = PhosphorPipeWireCli::waitForPropsEchoAndVerify(
        node, matchesRequest, PhosphorPipeWireCli::postWriteSettleMs(), "set-volume");
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
    auto* node = PhosphorPipeWireCli::resolveTarget(conn, target);
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
    const bool echoed = PhosphorPipeWireCli::waitForPropsEchoAndVerify(
        node, matchesRequest, PhosphorPipeWireCli::postWriteSettleMs(), "mute");
    // On match, the predicate guarantees observed == requested. On
    // timeout AND the node is still alive, read the live mute state so
    // the diagnostic shows what the daemon currently exposes. On
    // timeout AND the node has been removed mid-wait, reading muted()
    // would be UB — surface the requested value (the same value that
    // wouldn't echo) so the diagnostic stays self-consistent alongside
    // the "node removed during wait" message that
    // waitForPropsEchoAndVerify already emitted.
    // Default to the requested value so the two "show requested"
    // branches (echo confirmed OR node removed mid-wait) need no
    // explicit assignment. Only the timeout-with-live-node case
    // overwrites with the daemon's current live state.
    bool echoedMuted = muted;
    if (!echoed && nodePtr)
        echoedMuted = nodePtr->muted();
    out() << "set node " << nodeId << " (" << nodeName << ") muted = " << (muted ? "true" : "false")
          << ", echoed = " << (echoedMuted ? "true" : "false") << "\n";
    // On timeout, waitForPropsEchoAndVerify already logged the timeout
    // diagnostic; we just route through rc=1 to surface the failure.
    if (!echoed)
        return 1;
    return 0;
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
    const QString name = PhosphorPipeWireCli::resolveTargetToName(conn, trimmed);
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
    const int deadlineMs = PhosphorPipeWireCli::postMetadataSettleMs();
    if (!PhosphorPipeWireCli::waitForString(conn, changedSignal, accessor, name, deadlineMs, label)) {
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

/// Distinguishes commands that only read state (no in-flight write to
/// flush) from commands that issued a write (volume/mute/set-default-*).
/// Read-only commands skip the post-disconnect settle entirely: there is
/// nothing for the loop thread to flush, so the 50ms wait is pure
/// overhead. Write commands keep the settle so the loop thread has time
/// to drain any in-flight pw_loop_invoke before the destructor runs.
enum class CleanShutdownMode {
    ReadOnly,
    AfterWrite
};

/// Explicit disconnect + (in write mode) a brief settle so the loop
/// thread has time to flush any in-flight write before the connection
/// destructor runs. The destructor handles teardown safely on its own,
/// but routing through disconnectFromDaemon() first makes the teardown
/// order deterministic for the metadata/volume write paths the CLI
/// exercises.
///
/// Three cases skip the settle:
///   1. The connection never reached the connected state (connect
///      timeout) — disconnectFromDaemon is a documented no-op pre-connect.
///   2. `mode == ReadOnly` — list/default commands never issued a write,
///      so there is nothing for the loop thread to flush.
///   3. Usage-error early returns from main() pass ReadOnly because no
///      command body actually ran.
///
/// In every skip case the settle window would be pure overhead; tools
/// that probe a no-daemon host or run a quick `list` should return
/// promptly rather than burning 50ms of idle wait per invocation.
void cleanShutdown(PhosphorServicePipeWire::PipeWireConnection& conn, CleanShutdownMode mode)
{
    const bool wasConnected = conn.isConnected();
    conn.disconnectFromDaemon();
    if (!wasConnected || mode == CleanShutdownMode::ReadOnly)
        return;
    QEventLoop loop;
    QTimer::singleShot(PhosphorPipeWireCli::kDefaultShutdownSettleMs, &loop, &QEventLoop::quit);
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
        if (!PhosphorPipeWireCli::isKnownListKind(args.at(2))) {
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
    // Range-validate set-volume's pct at the usage layer so out-of-range
    // typos (`set-volume sink 150`, `set-volume sink -5`, `set-volume
    // sink nan`) fail fast with rc=64 instead of paying the 2s handshake
    // cost only to surface as a runtime error (rc=1) from cmdSetVolume.
    // The pct domain is documented in both usage() ("0..100") and the
    // README, so a value outside [0, 100] (or non-finite) is a malformed
    // argument, not a runtime failure: surfacing it as rc=64 here lets
    // wrapper scripts distinguish "you typed it wrong" from "the daemon
    // refused the write" without parsing stderr. cmdSetVolume keeps its
    // in-function range check as a belt-and-suspenders guard so the
    // function stays safe to call standalone (e.g. from a future
    // programmatic caller that doesn't route through main()'s dispatch).
    if (cmd == QLatin1String("set-volume")) {
        bool okPct = false;
        const double pct = args.at(3).toDouble(&okPct);
        if (!okPct) {
            err() << "set-volume pct must be a number, got '" << args.at(3) << "'\n";
            return usage();
        }
        if (!std::isfinite(pct) || pct < 0.0 || pct > 100.0) {
            err() << "set-volume pct must be a finite value in [0, 100], got " << pct << "\n";
            return usage();
        }
    }

    PhosphorServicePipeWire::PipeWireConnection conn;
    const int timeoutMs = PhosphorPipeWireCli::connectTimeoutMs();
    conn.connectToDaemon();
    // waitForConnect early-outs if the connection is already connected,
    // otherwise it blocks on a local QEventLoop driven by the
    // connectedChanged signal. Async handshakes via pw_loop_invoke /
    // queued signals can't complete synchronously, so the slot-driven
    // path is always what runs.
    if (!PhosphorPipeWireCli::waitForConnect(conn, timeoutMs)) {
        err() << "failed to connect to PipeWire daemon within " << timeoutMs << "ms\n";
        // Connect failed before any command body ran — no writes in
        // flight, so the settle is unneeded overhead.
        cleanShutdown(conn, CleanShutdownMode::ReadOnly);
        return 2;
    }
    PhosphorPipeWireCli::settleRegistry();

    int rc = 0;
    // Per-command shutdown mode. list/default are read-only; the
    // set-volume / mute / unmute / set-default-* commands all issue a
    // write through pw_loop_invoke and benefit from the loop-thread
    // settle so the destructor sees a drained queue.
    CleanShutdownMode shutdownMode = CleanShutdownMode::ReadOnly;
    if (cmd == QLatin1String("list")) {
        rc = cmdList(conn, args.at(2));
    } else if (cmd == QLatin1String("default")) {
        rc = cmdDefault(conn);
    } else if (cmd == QLatin1String("set-volume")) {
        // Arg-count, numeric parse, and [0, 100] range were all
        // validated pre-connect in the guard block above. The
        // toDouble() here just re-parses the already-validated token;
        // the bool-out is discarded because any failure would have
        // been routed through usage() before we ever reached the
        // connect handshake. cmdSetVolume keeps its own range check
        // as a belt-and-suspenders guard (returning rc=1 on out-of-
        // range, since by that point the connect cost has already
        // been paid).
        const double pct = args.at(3).toDouble();
        shutdownMode = CleanShutdownMode::AfterWrite;
        rc = cmdSetVolume(conn, args.at(2), pct);
    } else if (cmd == QLatin1String("mute") || cmd == QLatin1String("unmute")) {
        shutdownMode = CleanShutdownMode::AfterWrite;
        rc = cmdMute(conn, args.at(2), cmd == QLatin1String("mute"));
    } else if (cmd == QLatin1String("set-default-sink")) {
        // The accessor/setter lambdas capture `conn` by reference. They
        // outlive only within main()'s synchronous dispatch — cmdSetDefault
        // is called inline, drives a local QEventLoop, and returns before
        // we leave this scope, so the reference is always live. If the
        // dispatch ever moves to a queued/async model (e.g. spawning the
        // command on a worker thread or deferring through a QTimer), switch
        // to copy semantics or pass `conn` through cmdSetDefault parameters
        // so the lambdas don't dangle on the moved/destroyed connection.
        shutdownMode = CleanShutdownMode::AfterWrite;
        rc = cmdSetDefault(
            conn, args.at(2), "default sink", &PhosphorServicePipeWire::PipeWireConnection::defaultSinkNameChanged,
            [&conn]() {
                return conn.defaultSinkName();
            },
            [&conn](const QString& name) {
                conn.setDefaultSink(name);
            });
    } else if (cmd == QLatin1String("set-default-source")) {
        // Same lambda-capture caveat as set-default-sink above: by-reference
        // capture of `conn` is safe only because cmdSetDefault runs
        // synchronously within main()'s scope. Revisit if the dispatch
        // ever becomes async/queued.
        shutdownMode = CleanShutdownMode::AfterWrite;
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
        // No command body ran to completion; no in-flight write to
        // flush, so use the read-only shutdown mode.
        cleanShutdown(conn, CleanShutdownMode::ReadOnly);
        return 70; // EX_SOFTWARE
    }
    cleanShutdown(conn, shutdownMode);
    return rc;
}

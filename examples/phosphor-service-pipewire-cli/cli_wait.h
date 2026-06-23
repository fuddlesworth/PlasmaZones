// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "cli_env.h"

#include <functional>

class QString;

namespace PhosphorServicePipeWire {
class PipeWireConnection;
class PwNode;
} // namespace PhosphorServicePipeWire

namespace PhosphorPipeWireCli {

/// Wait for the connection to either reach the connected state or to
/// time out. Early-outs if `isConnected()` is already true; otherwise
/// blocks on a local QEventLoop driven by the slot-driven async
/// handshake. The CLI is a one-shot tool: the daemon either answers
/// promptly or we bail with an error.
bool waitForConnect(PhosphorServicePipeWire::PipeWireConnection& conn, int timeoutMs);

/// Many commands need the registry to settle (initial node enumeration)
/// before they can act. PipeWire fires every `global` event before the
/// `done` that completes the connected handshake, but our connect path
/// flips `connected` on the done event, so by the time we observe
/// connected the nodes are already enumerated. Still, give a short
/// extra tick for late-arriving SPA_PARAM_Props events (volumes,
/// mute) before we render output.
void settleRegistry(int extraMs = kDefaultPostReadSettleMs);

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
                               int timeoutMs, const char* label);

/// Signal-driven wait until `accessor()` equals `expected` or the
/// deadline elapses. Used for set-default-sink / set-default-source so
/// the CLI returns success only when WirePlumber has actually committed
/// the new default. `connectSignal` wires the change signal to the
/// loop's quit slot; unrelated emissions (the metadata observer firing
/// for a different key) just cause a re-check of the predicate, so we
/// keep waiting until either the value matches or the deadline elapses.
/// Returns true on match, false on timeout. Parallel to
/// waitForConnect's shape.
///
/// `label` is used for a per-helper timeout diagnostic on stderr so
/// failures self-report consistently with waitForPropsEchoAndVerify.
/// Callers (e.g. cmdSetDefault) may emit their own higher-level
/// diagnostic — the helper-level diagnostic supplies per-helper
/// visibility under PHOSPHOR_PW_LOG=debug-style introspection.
bool waitForString(PhosphorServicePipeWire::PipeWireConnection& conn,
                   void (PhosphorServicePipeWire::PipeWireConnection::*signal)(),
                   const std::function<QString()>& accessor, const QString& expected, int timeoutMs, const char* label);

} // namespace PhosphorPipeWireCli

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

namespace PhosphorPipeWireCli {

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

/// Read a positive-int env var override, or fall back to `fallbackMs`
/// on empty / malformed / non-positive values. Centralises the parse so
/// every settle-timeout knob has the same defensive behaviour: a typo'd
/// env value never silently routes through to 0 (which would defeat the
/// echo waits entirely).
int envOverrideMs(const char* name, int fallbackMs);

/// Parallel to envOverrideMs for positive double-valued env vars (e.g.
/// the volume-echo tolerance epsilon). Empty / malformed / non-finite /
/// non-positive values fall back to `fallback`; a typo'd env value
/// never silently routes through to 0 (which would defeat the echo
/// check entirely).
qreal envOverrideDouble(const char* name, qreal fallback);

/// Connect-timeout sourced from the env var so users hitting slow-boot
/// scenarios can override without recompiling.
int connectTimeoutMs();

/// Volume-echo tolerance: daemons may quantise linear amplitude on
/// SPA_PARAM_Props, so the set-volume predicate uses a non-zero epsilon.
/// Overridable via PHOSPHOR_PW_VOLUME_ECHO_EPSILON for hosts whose
/// daemon quantises more aggressively.
qreal volumeEchoEpsilon();

/// Post-write (volume/mute) settle deadline. Loaded CI hosts can extend
/// the window via PHOSPHOR_PW_POST_WRITE_SETTLE_MS without recompiling.
int postWriteSettleMs();

/// Post-metadata (set-default-sink/source) settle deadline. WirePlumber
/// round-trip varies with load; extend via
/// PHOSPHOR_PW_POST_METADATA_SETTLE_MS on slow hosts.
int postMetadataSettleMs();

} // namespace PhosphorPipeWireCli

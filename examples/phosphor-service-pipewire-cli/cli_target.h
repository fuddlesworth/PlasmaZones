// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>
#include <QString>

namespace PhosphorServicePipeWire {
class PipeWireConnection;
class PwNode;
} // namespace PhosphorServicePipeWire

namespace PhosphorPipeWireCli {

PhosphorServicePipeWire::PwNode* findNode(PhosphorServicePipeWire::PipeWireConnection& conn, uint id);

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
PhosphorServicePipeWire::PwNode* resolveTarget(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec);

/// Resolve a target spec to a node.name string, accepting numeric ids
/// by pre-resolving via findNode/resolveTarget. Without this, a user
/// typing `set-default-sink 55` would write the literal string "55" into
/// WirePlumber's default-metadata key. On miss, returns an empty string
/// and writes an error to stderr.
QString resolveTargetToName(PhosphorServicePipeWire::PipeWireConnection& conn, const QString& spec);

QString labelFor(PhosphorServicePipeWire::PwNode* node);

void printNode(PhosphorServicePipeWire::PwNode* node);

// Canonical audio media-class set the CLI recognises. Centralised here
// so isKnownListKind() / printNode() / cmdList()'s wanted-set / any
// future media-class check all iterate the same source-of-truth array
// instead of drift-prone duplicated literals.
//
// Note: these strings are also used inside the smoke tests and inside
// PwNodeModel's filtering. Consolidating them behind a single public
// header constant set in the library is a separate change that touches
// the library API; this CLI keeps a local table so it remains a
// stand-alone consumer of the public types.
inline constexpr const char* kAudioMediaClasses[] = {
    "Audio/Sink",
    "Audio/Source",
    "Stream/Output/Audio",
    "Stream/Input/Audio",
};

/// True if `mc` is one of the four audio media-class strings the CLI
/// recognises. Iterates kAudioMediaClasses so isKnownListKind /
/// printNode / any future call stays in lock-step with the table.
bool isAudioMediaClass(const QString& mc);

/// Map a `list` subcommand kind ("sinks" | "sources" | "streams") to
/// the matching set of media-class strings. Returns an empty set for
/// any unrecognised kind — callers should treat that as "unknown kind"
/// and either reject pre-connect (main) or emit a diagnostic
/// (cmdList). Centralises the kind→media-class mapping so adding a
/// new kind is a single-site edit instead of a lock-step pair across
/// isKnownListKind + cmdList's switch.
QSet<QString> kindToMediaClasses(const QString& kind);

/// True iff `kind` maps to a non-empty media-class set. Thin wrapper
/// around kindToMediaClasses for the pre-connect typo-check path.
bool isKnownListKind(const QString& kind);

} // namespace PhosphorPipeWireCli

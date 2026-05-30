// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
bool isKnownListKind(const QString& kind);

} // namespace PhosphorPipeWireCli

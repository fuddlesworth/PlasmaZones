// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSurface/DecorationProfileTree.h>

#include <QString>

namespace PlasmaZones {

/// The DecorationProfileTree root a decoration surface page owns. The three
/// surface pages each edit exactly one root subtree — "window" (+ .tiled/
/// .snapped/.floating), "osd", or "popup" (+ .snapAssist/.zoneSelector/
/// .layoutPicker/.cheatsheet) — so per-page Reset/Discard/dirty must be
/// scoped to that root or one surface's revert clobbers the others (the bug
/// this mapping fixes). The roots mirror the surfacePath prefixes the QML
/// pages bind (DecorationWindowsPage etc.) and the DecorationSupportedPaths
/// taxonomy.
/// Returns an empty string for the non-surface decoration leaves — the sets
/// library and the read-only shaders browser — whose Reset/Discard/dirty act
/// on the whole editable tree (every root at once).
QString decorationSurfaceRoot(const QString& page);

/// True iff @p path is @p root itself or a descendant of it ("window" owns
/// "window", "window.tiled", …; "osd" owns only "osd"). The dot guard keeps a
/// sibling root with a shared prefix from leaking in — there are none today,
/// but it costs nothing and documents the intent. An empty @p root matches
/// only the empty path; callers dispatch the whole-tree (non-surface) case
/// before calling this.
bool decorationPathInRoot(const QString& path, const QString& root);

/// True iff the two trees' direct overrides differ anywhere under @p root.
/// The baseline (path "") is never under a surface root, so it is correctly
/// excluded — no surface page edits the global baseline (decoration sets
/// carry none either; see decorationpagecontroller_sets.cpp). Walks the union
/// of both trees' overridden paths so an override PRESENT in one and ABSENT
/// in the other counts.
bool decorationRootDiffers(const PhosphorSurfaceShaders::DecorationProfileTree& current,
                           const PhosphorSurfaceShaders::DecorationProfileTree& baseline, const QString& root);

} // namespace PlasmaZones

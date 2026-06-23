// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorShaders/ShaderEntryPoint.h>

#include <plasmazones_rendering_export.h>

#include <QList>
#include <QString>

namespace PlasmaZones {

/// The harness scaffold for zone fragment shaders authored against the T1.4
/// entry-point convention (scope B): a pack defines only `vec4 pZone(ZoneCtx)`
/// or `vec4 pImage(vec2)` (plus helpers) and the harness supplies `#version`,
/// the `common.glsl` include, the in/out declarations, and `main()`. Packs that
/// keep their own `main()` are passed through untouched.
///
/// Zone shaders run on the daemon Qt-RHI path only (no kwin-effect), so unlike
/// the animation entry shapes this scaffold has a single runtime to satisfy.

/// The lines prepended above an entry-only pack's body: `#version`, the shared
/// include (which declares `ZoneCtx`, the zone UBO, and the helpers the
/// generated `main()` calls), and the fragment in/out. Newline-terminated.
PLASMAZONES_RENDERING_EXPORT QString zoneEntryPrologue();

/// The entry candidates the harness recognises, in priority order: `pZone`
/// (per-zone dispatch loop) then `pImage` (full-frame). Each carries the
/// `main()` the harness appends when that function is defined.
PLASMAZONES_RENDERING_EXPORT QList<PhosphorShaders::EntryCandidate> zoneEntryCandidates();

/// Assemble a zone fragment shader's raw on-disk source into the source the
/// bake layer should expand + compile.
///
///   • Raw source that already defines `main()` → returned unchanged (every
///     bundled pack today; the escape hatch).
///   • Otherwise the prologue is prepended and, if the body defines a
///     recognised entry function, the matching `main()` is appended.
///   • A body with neither `main()` nor a recognised entry → prologue +
///     body, leaving the missing-`main()` error for the compiler (mapped back
///     to the author via the resolver `#line` legend).
///
/// MUST be applied identically on the live load and the warm-bake, and folded
/// into the bake-cache key, or the two paths would disagree on the compiled
/// source while colliding on one key.
PLASMAZONES_RENDERING_EXPORT QString assembleZoneEntrySource(const QString& rawFragmentSource);

} // namespace PlasmaZones

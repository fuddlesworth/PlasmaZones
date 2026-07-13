// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file types.h
/// Umbrella over the effect's POD state types.
///
/// The definitions live in two headers because they are two unrelated concerns
/// that never reference each other:
///   - surface_types.h    — the surface/decoration fold (compiled packs, per-window
///                          multipass state, WindowDecoration).
///   - transition_types.h — the window animation transitions (cached shaders and
///                          textures, ShaderTransition and its timing, restore
///                          suppression).
///
/// This header stays as the single include point so every existing consumer keeps
/// working unchanged. A TU that genuinely needs only one half should include that
/// half directly and skip the other's dependencies (PhosphorSurface vs
/// PhosphorAnimation).

#include "surface_types.h"
#include "transition_types.h"

#include <QRect>
#include <QString>

namespace PlasmaZones {

/// A single virtual screen subdivision within a physical monitor.
///
/// Virtual screens divide a physical monitor into independent sub-screens,
/// each with its own zones, autotile state, etc. The daemon manages
/// definitions; the effect fetches them via D-Bus and resolves positions.
///
/// Named `EffectVirtualScreenDef` to avoid collision with the daemon's
/// `PhosphorScreens::VirtualScreenDef` (which has many more fields).
struct EffectVirtualScreenDef
{
    QString id; ///< e.g., "Dell:U2722D:115107/vs:0"
    QRect geometry; ///< Absolute geometry in global compositor coords
};

} // namespace PlasmaZones

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {
struct SurfaceShaderEffect;

// Resolve opt-in window-state parameters on a copy of the authored parameters.
// maximized also covers fullscreen. Returns false when a declared, enabled
// hideWhenMaximized removes this pack from the rendered chain; otherwise an
// enabled squareWhenMaximized sets its declared cornerRadius to zero. Without
// those flags, user-authored profiles retain their configured shape and layers.
PHOSPHORSURFACE_EXPORT bool resolveWindowStateParams(const SurfaceShaderEffect& effect, QVariantMap& friendlyParams,
                                                     bool maximized);
}

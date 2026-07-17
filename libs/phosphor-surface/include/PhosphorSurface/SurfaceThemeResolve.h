// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QColor>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {

class SurfaceShaderEffect;

/**
 * @brief Theme colours a host supplies to resolveThemeParamColors().
 *
 * The host (daemon overlay path or KWin window-decoration path) resolves these
 * from its own colour source — the daemon reads QGuiApplication::palette() plus
 * its highlight/inactive settings; the compositor reads the same values plumbed
 * over D-Bus — and passes them in so the flag→colour synthesis stays identical
 * on both paths.
 */
struct SurfaceThemeColors
{
    QColor accent; ///< System accent / highlight (useSystemAccent active border).
    QColor inactive; ///< Unfocused accent (useSystemAccent inactive border).
    QColor background; ///< Theme window background (useThemeNeutral lerp base, useThemeTint halo).
    QColor foreground; ///< Theme window foreground (useThemeNeutral lerp target).
};

/**
 * @brief Synthesise theme-derived colours into a pack's friendly param map,
 *        in place, honouring the host-consumed flags a surface pack may declare.
 *
 * - Border pack: `useThemeNeutral` (with `frameContrast`) fills active/inactive
 *   with a neutral frame-contrast colour (background lerped toward foreground);
 *   otherwise `useSystemAccent` fills them with the system accent / inactive.
 *   Neutral wins over accent when both are engaged.
 * - Glow / shadow pack: `useThemeTint` replaces the halo colour with the theme
 *   background, preserving the pack's own colour alpha (its intensity knob).
 *
 * Only params the pack actually declares (via @p effect.parameters) are touched,
 * so a pack without a given flag is left untouched and a flag left at its
 * default (or off) leaves the caller's explicit colours in place. Shared by the
 * daemon overlay path and the KWin window-decoration path so both resolve
 * colours identically. Call this on the friendly param map BEFORE translating it
 * to slot-keyed uniforms (the flags are host-consumed and never reach the shader).
 */
PHOSPHORSURFACE_EXPORT void resolveThemeParamColors(const SurfaceShaderEffect& effect, QVariantMap& friendlyParams,
                                                    const SurfaceThemeColors& theme);

} // namespace PhosphorSurfaceShaders

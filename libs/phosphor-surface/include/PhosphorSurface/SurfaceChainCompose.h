// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QVariantMap>

namespace PhosphorSurfaceShaders {

struct SurfaceShaderEffect;

/**
 * @brief Resolve one pack's outer-margin request, in LOGICAL pixels.
 *
 * A pack declares an outer effect (the glow pack's `glowSize`, the shadow
 * pack's spread) by naming one of its own float parameters in
 * `paddingParam`. The host inflates its capture canvas by the chain's LARGEST
 * request so that effect has real transparent room to draw into — the
 * "extended FBO".
 *
 * Resolution order is the per-surface override first, then the parameter's
 * declared default. A pack with no `paddingParam`, or one naming a parameter
 * it does not declare, requests 0.
 *
 * This is deliberately the RAW request: callers clamp it themselves, because
 * they need different types. The compositor's capture canvas is integer
 * device pixels (`qCeil` then an int clamp); the daemon's QML host works in
 * fractional logical px. Both bound the result by
 * `kMaxDecorationOuterPaddingPx` so a typo'd or hostile pack cannot demand an
 * absurd canvas.
 *
 * Extracted because the identical resolution ran in three places and had
 * already drifted in type between two of them.
 */
PHOSPHORSURFACE_EXPORT double paddingRequest(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams);

/**
 * @brief Build the QML stage map for one surface-shader chain stage.
 *
 * The shape `src/shared/SurfaceDecoration.qml` consumes, one entry per resolved
 * pack: `source` / `vertexSource` (file:// urls), `preamble` (the generated
 * `#define p_<id> …` block), `params` (the translated
 * `customParamsN_*` / `customColorN` slot map), `animated` (gates that
 * stage's per-frame iTime tick), and the multipass set.
 *
 * @p resolvedParams is the pack's FRIENDLY parameter map after the host has
 * applied its own resolutions — `resolveThemeParamColors` for theme-derived
 * colours, plus any host-specific injection such as the card corner radius.
 * Those stay at the call site because they are host state, not pack data.
 *
 * Multipass fields are emitted only when the pack both opts into multipass
 * and carries at least one resolved buffer path. The registry resolves
 * `builtin:` tokens to absolute paths and clears the whole list fail-closed
 * when one cannot be located, so that gate is also what keeps a pack with an
 * unresolvable builtin on the single-pass path rather than handing empty
 * paths to the shader item. Single-pass stages carry `multipass: false` and
 * nothing further, leaving the item's own defaults untouched.
 *
 * Used by the daemon overlay-decoration host and by the settings app's
 * decoration preview, which must compose a stage identically or the preview
 * stops predicting what the daemon draws. The kwin-effect compositor path
 * builds GL uniform value arrays instead of a stage map and shares only
 * paddingRequest() above.
 *
 * Returns an EMPTY map for an effect that is not `isValid()` (no id, or no
 * fragment shader — which is also what the path-traversal guard leaves behind
 * when it rejects a pack's declared shader). Hosts must treat that as "skip
 * this stage" rather than appending it, or they add a stage with no source.
 */
PHOSPHORSURFACE_EXPORT QVariantMap composeStageMap(const SurfaceShaderEffect& effect,
                                                   const QVariantMap& resolvedParams);

} // namespace PhosphorSurfaceShaders

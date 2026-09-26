// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/phosphorsurface_export.h>

#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {

struct SurfaceShaderEffect;
class SurfaceShaderRegistry;

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
 * fractional logical px. Both bound the result to
 * `[0, kMaxDecorationOuterPaddingPx]` so a typo'd or hostile pack cannot demand
 * an absurd canvas. The ZERO floor is a caller's job too: a declared default or
 * a stored override may be negative, and this returns such a value verbatim
 * rather than guessing which way a caller wants to interpret it.
 *
 * Extracted because the identical resolution ran in three places and had
 * already drifted in type between two of them.
 */
PHOSPHORSURFACE_EXPORT double paddingRequest(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams);

/**
 * @brief Build the QML stage map for one surface-shader chain stage.
 *
 * The shape phosphor-surface-quick's `SurfaceDecoration.qml` consumes, one entry per resolved
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
 * Used by the daemon overlay-decoration host, by the shell's own chrome, and by the
 * settings app's decoration preview, all three of which must compose a stage
 * identically or the preview stops predicting what the daemon draws. One
 * deliberate exception: the two live hosts inject the chain's bottom-corner answer
 * into every stage's params before calling this, and the preview does not, because
 * it composes ONE pack at a time and so has no chain to reconcile. It injects no
 * card corner radius either, but for a reason of its own rather than this one: its
 * stand-in card is deliberately square-cornered so the pack owns the radius, and
 * injecting one would show a rounding real windows will not get (see
 * decorationpreviewcontroller.cpp). The consequence of the bottom-corner omission
 * is worth knowing: previewing a pack that is not first in its chain shows that
 * pack's own bottom-corner setting, while the real surface shows the chain's.
 *
 * The kwin-effect compositor path builds GL uniform value arrays instead of a
 * stage map, so it never calls this builder; it shares paddingRequest() above and
 * chainRoundBottomCorners() below.
 *
 * Returns an EMPTY map for an effect that is not `isValid()` (no id, or no
 * fragment shader — which is also what the path-traversal guard leaves behind
 * when it rejects a pack's declared shader). Hosts must treat that as "skip
 * this stage" rather than appending it, or they add a stage with no source.
 *
 * @p blurScaleMultiplier is the user's decoration blur-quality tier
 * (Decorations.Performance.BlurScaleMultiplier). It multiplies every declared
 * buffer scale, `bufferScale` and each entry of `bufferScales`, and the product is
 * bounded into [kMinBufferScale, kMaxBufferScale] exactly as the compositor's
 * clampedBufferScale() does. This is the daemon-side counterpart of that
 * chokepoint, and the reason it belongs here rather than in each host is that the
 * setting is a GLOBAL blur-quality tier. For a while the compositor honoured it
 * while nothing on this path read it at all, so the same pack rendered at two
 * densities depending on whether it decorated a window or an OSD.
 *
 * Defaulted to 1.0, the identity, for a caller with no settings to offer.
 */
PHOSPHORSURFACE_EXPORT QVariantMap composeStageMap(const SurfaceShaderEffect& effect, const QVariantMap& resolvedParams,
                                                   qreal blurScaleMultiplier = 1.0);

/**
 * @brief The bottom-corner answer every pack in one chain has to draw to.
 *
 * A pane's silhouette belongs to the CHAIN, not to any single pack. A backdrop
 * pack rounds its slab, a border pack traces an outline around that slab, and a
 * glow or shadow pack hugs the same outline from outside. Let them disagree about
 * whether the bottom corners are round and the user gets a border curving through
 * empty space over a squared-off pane, or a shadow rounding a corner the pane
 * gave up. Per-pack defaults cannot prevent it, because the two packs are
 * configured on separate pages and nothing there says they are describing one
 * shape.
 *
 * So the host resolves ONE answer for the whole chain and injects it into every
 * stage, the way the daemon overlay path already injects its card corner radius.
 * In order:
 *
 *   1. the value stored against the FIRST pack in chain order that carries a
 *      USABLE one. Where two packs both carry a stored value they disagree with
 *      no right answer, and chain order is what keeps the result deterministic
 *      rather than dependent on map iteration. An explicitly-null or absent
 *      entry is not a choice, so it falls through to that pack's own default.
 *   2. otherwise the declared default of the first pack that declares the
 *      control WITH a default, which is what settles a third-party pack shipping
 *      a different default from the bundled ones. A pack declaring the control
 *      and no default abstains rather than voting false.
 *   3. otherwise an invalid QVariant, meaning no pack in this chain draws an
 *      outline at all and the host injects nothing.
 *
 * @p allPackParams is the post-flatten `effectiveParameters()` map, shaped
 * { packId -> { paramId -> value } }. It carries whatever a user or a preset set,
 * so a pack with nothing set never outvotes one that has. Two caveats a caller
 * should not have to discover: a PRESET's value is indistinguishable here from a
 * value the user typed, so a preset on a late pack can decide the whole chain's
 * outline; and a host that rewrites a pack's entry before calling — the
 * compositor does, both for a matched rule override and for its easy-mode layers
 * — hands that pack's vote to whatever it wrote, because the tree's value for it
 * is gone by then.
 *
 * Injecting the result into a pack that does not declare the control is harmless.
 * translateSurfaceParams and resolveSurfaceParamValues emit a lane only for
 * declared parameters, so the key is dropped for any pack without it, and hosts
 * need no per-pack test before inserting.
 *
 * Packs the registry cannot resolve are skipped rather than treated as declaring
 * nothing, so an uninstalled pack sitting in a stored chain does not get a vote.
 *
 * Only the bottom-corner SWITCH is resolved chain-wide. `edgeSoftness` is
 * deliberately left per-pack: a band's anti-alias feather and a backdrop slab's
 * are different quantities on different geometry, which is why the two families
 * declare different ranges for it, and unifying them would soften one to match
 * the other rather than making them agree about a shape.
 */
PHOSPHORSURFACE_EXPORT QVariant chainRoundBottomCorners(const SurfaceShaderRegistry& registry, const QStringList& chain,
                                                        const QVariantMap& allPackParams);

/// The parameter id `chainRoundBottomCorners` resolves and hosts inject under.
/// Shared so the four call sites cannot drift on the spelling.
PHOSPHORSURFACE_EXPORT QString roundBottomCornersParamId();

} // namespace PhosphorSurfaceShaders

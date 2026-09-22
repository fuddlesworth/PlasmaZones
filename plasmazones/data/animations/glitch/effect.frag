// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glitch transition — operates on the rendered surface (uTexture0,
// binding 7) by sampling the captured surface with per-block UV
// displacement and per-channel RGB offset. The previous stub built
// `r/g/b` from `smoothstep(0, 1, uv)` (a centred radial mask) —
// visually a dim white circle, not a glitch. This version reads the
// surface directly and produces the displaced-channel chromatic
// aberration the metadata advertises. `iTime` is the per-leg [0,1]
// progress driven by SurfaceAnimator's shaderTime AnimatedValue —
// `sin(iTime*pi)` peaks the glitch mid-transition and settles at
// both endpoints regardless of direction.

#include <noise.glsl>

vec4 pTransition(vec2 uv, float t)
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.

    // Glitch peaks mid-leg (sin shape) regardless of direction so both
    // show and hide get the same "rip apart, then settle / settle, then
    // rip apart" feel without the shader needing to know the leg sign.
    // iTime is the per-leg [0,1] progress: iTime 0 → strength 0
    // (start of leg), iTime 1 → strength 0 (end of leg), iTime 0.5 →
    // peak.
    float visibility = clamp(iTime, 0.0, 1.0);
    float strength = p_intensity * sin(visibility * 3.14159);

    // Strength collapses to zero at both leg endpoints (sin is 0 at
    // visibility 0 and 1), where displacement and rgbSplit*strength
    // also vanish — every per-channel sample resolves to the same UV.
    // Skip the three-tap chromatic-aberration path and hand back a
    // single sample so the endpoint frames don't pay the redundant
    // texture cost on every fragment.
    if (strength < 1e-4) {
        return surfaceColor(uv);
    }

    // `blockSize` means block edge as a fraction of the screen — the
    // iAnchorSize / iSurfaceScreenPos.zw factor converts "fraction of
    // the screen" into "fraction of the surface" so block pixel size
    // stays constant across popup vs. maximized windows. Floors guard
    // against the pre-first-frame (0,0) state of either uniform.
    float bs = max(p_blockSize, 0.01);
    vec2 block = floor(uv * max(iAnchorSize, vec2(1.0))
                          / (bs * max(iSurfaceScreenPos.zw, vec2(1.0))));
    // Quantise the jitter to per-leg-frame buckets that bump every ~10
    // frames (at 60 Hz, ~6 buckets per second of leg time). Drive off
    // `iFrame` rather than `iTime` because iFrame is monotonically
    // increasing in BOTH leg directions — SurfaceAnimator runs iTime
    // 1→0 on the reverse leg, which would make `floor(iTime * 10.0)`
    // tick BACKWARDS through the same bucket sequence on hide and
    // produce a visible reverse-replay rather than a fresh jitter
    // pattern.
    float blockNoise = niriHash(block + floor(float(iFrame) * 0.1));

    float displacement = 0.0;
    if (blockNoise > (1.0 - strength * 0.5)) {
        displacement = (niriHash(block * 2.0) - 0.5) * strength * 0.2;
    }

    vec2 uvR = uv + vec2(displacement + p_rgbSplit * strength, 0.0);
    vec2 uvG = uv + vec2(displacement, 0.0);
    vec2 uvB = uv + vec2(displacement - p_rgbSplit * strength, 0.0);

    // Sample the surface with per-channel offset for the chromatic
    // aberration. Texture sampler has clampToEdge so off-surface UVs
    // bleed edge colour rather than wrapping or going transparent.
    // Qt Quick uses premultiplied-alpha blending, so un-premultiply
    // each sample before extracting the single channel, then
    // re-premultiply against the chosen alpha.
    vec4 sR = surfaceColor(uvR);
    vec4 sG = surfaceColor(uvG);
    vec4 sB = surfaceColor(uvB);
    // Output alpha is the MAX of the three sample alphas, not just sG.a.
    // When chromatic offset lands R or B on opaque pixels but G on a
    // transparent edge, using sG.a alone would zero the entire pixel and
    // erase the valid R+B chromatic split (visible "missing pixels" at
    // window edges during glitch). max() preserves any sample's contribution
    // and keeps the chromatic-aberration intent intact at silhouette edges.
    float a = max(max(sR.a, sG.a), sB.a);
    float r = (sR.a > 0.001) ? sR.r / sR.a : 0.0;
    float g = (sG.a > 0.001) ? sG.g / sG.a : 0.0;
    float b = (sB.a > 0.001) ? sB.b / sB.a : 0.0;
    return vec4(r * a, g * a, b * a, a);
}

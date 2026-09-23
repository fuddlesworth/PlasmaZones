// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wobbly-move vertex shader — thin consumer of the generic soft-body
// control lattice. All the physics (neighbour-coupled springs, the grip
// constraint, the settle) runs host-side and arrives as iMoveMesh: a 4x4
// grid of node deflections in logical px. This stage does only what KWin's
// wobbly effect does at render time — a bicubic Bezier patch over the 4x4
// control points — so the coarse simulated lattice becomes a smooth,
// continuously-curving deformation across the fine render mesh. Because
// the lattice is neighbour-coupled, a pull at the grabbed node propagates
// through the whole sheet as a wave: the entire window participates and
// wobbles like cloth, no rigid tail. p_strength scales the deflection;
// p_maxBend caps a violent fling.
//
// Dual-branch: iMoveMesh is a real member of the AnimationUniforms
// transition tail on the UBO branch these days, so the same Bezier
// deformation runs on both ABIs — the compositor feeds it from its live
// drag simulation, and a preview host steps the same mesh_sim integrator
// against a simulated glide. Only the texcoord flip and the position
// emission differ per branch.

#version 450

#include <animation_uniforms.glsl>

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 vTexCoord;

#ifdef PLASMAZONES_KWIN
uniform mat4 modelViewProjectionMatrix;
#endif

// Cubic Bernstein basis for a Bezier patch parameter in [0,1].
vec4 bernstein(float u) {
    float iu = 1.0 - u;
    return vec4(iu * iu * iu,
                3.0 * u * iu * iu,
                3.0 * u * u * iu,
                u * u * u);
}

// Bicubic Bezier evaluation of the 4x4 iMoveMesh lattice at (s,t), node
// (i,j) = iMoveMesh[i + 4*j]. Returns the interpolated deflection (px).
vec2 sampleMesh(float s, float t) {
    vec4 bs = bernstein(s);
    vec4 bt = bernstein(t);
    vec2 acc = vec2(0.0);
    for (int j = 0; j < 4; ++j) {
        vec2 row = vec2(0.0);
        for (int i = 0; i < 4; ++i) {
            row += bs[i] * iMoveMesh[i + 4 * j];
        }
        acc += bt[j] * row;
    }
    return acc;
}

void main() {
#ifdef PLASMAZONES_KWIN
    // Card uv with y = 0 at the window top (KWin Y-flips window-quad
    // texcoords on upload; re-apply the flip, same as the flow pack). The
    // lattice rows run top (row 0) to bottom, so cuv indexes it directly.
    vec2 cuv = vec2(texCoord.x, 1.0 - texCoord.y);
#else
    // The Qt-RHI quad's texCoord is Y-down already (no re-flip) but spans
    // the captured padded canvas — fold the anchor sub-rect so cuv is CARD
    // space with the halo band outside [0,1], matching the kwin
    // window-quad convention above. Identity rect (a bare-card capture)
    // makes this a passthrough.
    vec2 cuv = (texCoord - iAnchorRectInTexture.xy) / max(iAnchorRectInTexture.zw, vec2(1.0e-5));
#endif

    // Clamp the mesh sample to the frame so halo vertices (cuv past [0,1]
    // in the decoration margin) rigidly follow the nearest frame edge's
    // deflection instead of extrapolating the Bezier patch.
    vec2 lag = sampleMesh(clamp(cuv.x, 0.0, 1.0), clamp(cuv.y, 0.0, 1.0)) * clamp(p_strength, 0.0, 3.0);

    // Cap so a violent fling cannot fold the window over itself; the clamp
    // preserves direction.
    float m = length(lag);
    float cap = max(p_maxBend, 8.0);
    if (m > cap) {
        lag *= cap / m;
    }

    vTexCoord = cuv;
#ifdef PLASMAZONES_KWIN
    gl_Position = modelViewProjectionMatrix * vec4(position + lag, 0.0, 1.0);
#else
    // Qt-RHI path: the logical-px deflection converts to clip units at
    // 2 / iResolution — see flow's #else arm for the full contract. The
    // item runs the pack's geometryGrid mesh, so the Bezier sheet curves
    // here exactly as the kwin window-quad grid does.
    gl_Position = qt_Matrix * vec4(position + lag * 2.0 / max(iResolution, vec2(1.0)), 0.0, 1.0);
#endif
}

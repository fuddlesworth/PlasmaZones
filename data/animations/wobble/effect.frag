// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wobbly-move fragment shader — plain decorated sampling; all the work
// happens in the vertex stage's velocity-lag deformation. Samples through
// surfaceColor() so the full decoration chain (blur pane, border, glow)
// wobbles with the window as one object; the composite's own premultiplied
// alpha provides the anti-aliased silhouette, so no card mask is applied.

vec4 pTransition(vec2 uv, float t) {
    // Sample the decorated composite directly. NO [0,1] card mask: the grid
    // now extends past the frame into the decoration halo band (cuv < 0 or
    // > 1 there), and the composite's own premultiplied alpha is the correct
    // silhouette — including the soft halo edge. A mask clipped to [0,1]
    // would cut the glow / shadow / fireflies margin off at the frame.
    return surfaceColor(uv);
}

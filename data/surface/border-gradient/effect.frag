// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Gradient border surface shader — the Border pack's rounded-rect clip +
// border band, with the band coloured by a STILL linear two-colour
// gradient across the frame at a configurable angle. The static sibling
// of the sweep pack: same band geometry, no time dependency, so the
// compositor never has to repaint the window for it. Dims when the
// surface is unfocused, matching the family's focus cue.

#version 450
#include <surface_lib.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    if (surfaceFrameDegenerate()) {
        fragColor = tex;
        return;
    }

    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    float width = p_borderWidth * uSurfaceScale;
    float radius = (p_cornerRadius + p_borderWidth) * uSurfaceScale;

    FrameSDF fs = frameSdf(p, radius);
    float insideMask = 1.0 - smoothstep(-aa, aa, fs.d);
    float edge = smoothstep(-width - aa, -width + aa, fs.d);

    // Frame-normalised position projected onto the gradient direction, so
    // the blend spans the frame corner to corner at any angle and does not
    // stretch with the window's aspect.
    vec2 fuv = frameUv(p);
    float ang = radians(p_gradientAngle);
    vec2 dir = vec2(cos(ang), sin(ang));
    float t = smoothstep(0.0, 1.0, dot(fuv - 0.5, dir) + 0.5);
    vec4 band = mix(p_colorA, p_colorB, t);

    // Focus cue: full-strength band on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    fragColor = borderComposite(tex, band, edge, insideMask);
}

#version 450

// Opacity and tint. Fades the surface and optionally washes it with a colour.
// Works in premultiplied-alpha space (surfaceTexel is premultiplied): the tint
// is scaled by the source coverage and the opacity scales rgb and alpha
// together, so the output stays a valid premultiplied texel.
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 c = surfaceTexel(vTexCoord);

    // The tint colour's own alpha scales the wash alongside the strength param,
    // so a translucent tint colour tints more gently.
    float tint = clamp(p_tintStrength, 0.0, 1.0) * clamp(p_tintColor.a, 0.0, 1.0);
    if (tint > 0.001) {
        // Premultiply the tint by the surface's coverage so it only lands where
        // the surface is opaque and the mix stays premultiplied.
        vec3 washed = p_tintColor.rgb * c.a;
        c.rgb = mix(c.rgb, washed, tint);
    }

    // Dim the whole premultiplied texel.
    c *= clamp(p_opacity, 0.0, 1.0);

    fragColor = c;
}

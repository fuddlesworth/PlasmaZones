// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Slipstream — the strip leg of the phosphor set, built on the
// family's defining move: the CONTENT ITSELF becomes the light. Where
// phosphor-stream pours a window into its zone as separated luminous
// streams, Slipstream does the same to the whole strip while it scrolls:
// the content separates into horizontal streams of itself that lag and bow
// behind the motion on their own per-stream clocks, energized and pinched
// toward their centerlines, with the brand gradient (cyan #22D3EE → blue
// #3B82F6 → purple #A855F7 → rose #F43F5E) glowing along the seams between
// them and ember sparks shed in the wake. As the spring settles the streams
// reunite into the crisp image — the settled strip carries no residue,
// which is exactly the strip contract's identity requirement.
//
// Strip contract (strip_transition.glsl): iTime is SECONDS, not progress;
// every term below keys off iStripMotion, which converges to zero at
// settle.
#include <strip_transition.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose. Same
// tunable-slot shape as desktop-phosphor: the p_color* defines resolve to
// the customColors pool the strip pass binds.
vec3 fluxGradient(float t) {
    vec3 cyan = length(p_colorCyan.rgb) > 0.01 ? p_colorCyan.rgb : vec3(0.133, 0.827, 0.933);
    vec3 blue = length(p_colorBlue.rgb) > 0.01 ? p_colorBlue.rgb : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose = length(p_colorRose.rgb) > 0.01 ? p_colorRose.rgb : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

vec4 pTransition(vec2 uv, float t) {
    float m = stripMask(uv, 0.03);
    // Signed velocity in output-widths per second; the sign is the direction
    // the drawn content is travelling (the offset decays toward zero).
    float v = iStripMotion.w;
    float speed = abs(v);
    // The strip's ENERGY: how far it has slipped into stream form. Ramps in
    // on a moderate scroll, saturates on a flick, and is the single factor
    // every term below scales by — at zero the streams have reunited and
    // the frame is the identity image.
    float e = smoothstep(0.03, 0.35, speed) * m;
    vec4 base = getStripColor(uv);
    if (e < 1.0e-3) {
        return base;
    }
    float dir = sign(v);

    // ── The stream field ──
    // The strip divides into horizontal streams inside its work area. Each
    // stream runs on its own slightly offset clock (per-stream hash), so
    // under motion the streams visibly desynchronize instead of shearing as
    // one block — phosphor-stream's signature.
    vec2 s = stripUv(uv);
    float n = max(p_streams, 3.0);
    float band = floor(s.y * n);
    float by = fract(s.y * n) - 0.5; // -0.5 .. 0.5 within the stream
    vec2 br = hash22(vec2(band, 11.7));

    // Per-stream lag: content in a stream has not caught up with the strip's
    // committed position, so it is sampled from behind along the travel
    // direction. The lag bows within the stream (stronger toward its edges)
    // so each ribbon arcs rather than shifting rigidly.
    float lagMag = e * p_separation * 0.055 * (0.35 + 0.65 * br.x) * (1.0 + 1.2 * by * by);

    // Pinch toward the stream's centerline: the ribbons visibly thin apart
    // as they separate, which is what makes the strip read as streams of
    // content instead of a sheared image.
    float pinch = 0.35 * e * p_separation;
    float syPinched = (band + 0.5 + by * (1.0 - pinch)) / n;
    float yScale = (iStripRect.w > 0.0 && iResolution.y > 0.0) ? iStripRect.w / iResolution.y : 1.0;

    vec2 su = uv;
    su.x += dir * lagMag;
    su.y += (syPinched - s.y) * yScale;
    vec4 c = getStripColor(su);

    // ── Energize ──
    // The displaced content brightens on its own per-stream level, the
    // seams between streams fall into shadow, and the brand gradient glows
    // along them — each stream carrying its own stop, drifting slowly along
    // its length so the color reads as flow.
    float seam = smoothstep(0.30, 0.5, abs(by));
    vec3 seamGlow = fluxGradient(fract(br.y + s.x * 0.35 + iTime * 0.05 * dir)) * seam * e * p_glow * 1.1;
    float seamShade = 1.0 - 0.4 * e * seam;
    vec3 energized = c.rgb * (1.0 + 0.45 * e * p_glow * (0.4 + 0.6 * br.y)) * seamShade + seamGlow;

    // ── Ember sparks shed in the wake ──
    vec3 ember = vec3(0.0);
    if (p_embers > 0.01) {
        float aspect = resolutionSafe().x / max(resolutionSafe().y, 1.0);
        vec2 g = uv * vec2(34.0 * aspect, 34.0);
        g.x += dir * iTime * (2.0 + 12.0 * speed);
        vec2 cell = floor(g);
        vec2 rnd = hash22(cell);
        if (rnd.x > 1.0 - 0.4 * p_embers) {
            vec2 sparkPos = 0.15 + 0.7 * hash22(cell + 17.0);
            vec2 d = (g - cell - sparkPos) * vec2(0.55, 1.0); // stretched along x
            float twinkle = 0.6 + 0.4 * sin(iTime * (4.0 + 6.0 * rnd.y) + rnd.y * 6.2831);
            float spark = smoothstep(0.22, 0.0, length(d)) * twinkle;
            ember = fluxGradient(rnd.y) * spark * e * p_embers * 1.3;
        }
    }

    // Cross-fade the whole transformation by energy so a gentle scroll only
    // whispers into stream form and settle is exactly the identity.
    return vec4(mix(base.rgb, energized, clamp(e * 1.15, 0.0, 1.0)) + ember, base.a);
}

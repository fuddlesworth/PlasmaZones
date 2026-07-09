// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Cube — the outgoing and incoming desktops rotate past each other as
// the two visible faces of a spinning cube, with a floor reflection. The iconic
// compiz/KWin desktop-cube switcher. Ported from GL-Transitions "cube" by gre
// (https://github.com/gl-transitions/gl-transitions, MIT). progress -> t; the
// persp/unzoom/reflection/floating uniforms map to the p_* params.
#include <desktop_transition.glsl>

#ifdef PLASMAZONES_KWIN
vec2 pz_cube_project(vec2 p) {
    return p * vec2(1.0, -1.2) + vec2(0.0, -p_floating / 100.0);
}

bool pz_cube_inBounds(vec2 p) {
    return all(lessThan(vec2(0.0), p)) && all(lessThan(p, vec2(1.0)));
}

vec4 pz_cube_bgColor(vec2 pfr, vec2 pto) {
    vec4 c = vec4(0.0, 0.0, 0.0, 1.0);
    pfr = pz_cube_project(pfr);
    if (pz_cube_inBounds(pfr)) {
        c += mix(vec4(0.0), getFromColor(pfr), p_reflection * mix(1.0, 0.0, pfr.y));
    }
    pto = pz_cube_project(pto);
    if (pz_cube_inBounds(pto)) {
        c += mix(vec4(0.0), getToColor(pto), p_reflection * mix(1.0, 0.0, pto.y));
    }
    return c;
}

vec2 pz_cube_xskew(vec2 p, float persp, float center) {
    float x = mix(p.x, 1.0 - p.x, center);
    return ((vec2(x, (p.y - 0.5 * (1.0 - persp) * x) / (1.0 + (persp - 1.0) * x))
             - vec2(0.5 - distance(center, 0.5), 0.0))
                * vec2(0.5 / distance(center, 0.5) * (center < 0.5 ? 1.0 : -1.0), 1.0)
            + vec2(center < 0.5 ? 0.0 : 1.0, 0.0));
}
#endif // PLASMAZONES_KWIN

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    float uz = p_unzoom * 2.0 * (0.5 - distance(0.5, t));
    vec2 p = -uz * 0.5 + (1.0 + uz) * uv;
    // At the exact endpoints one face is zero-width, so its skew divides by zero
    // and yields inf/NaN coords. That is intentional (canonical GL-Transitions
    // "cube"): the inf face fails pz_cube_inBounds and the other, full-width face
    // is checked first and drawn, so no black frame results.
    vec2 fromP = pz_cube_xskew((p - vec2(t, 0.0)) / vec2(1.0 - t, 1.0), 1.0 - mix(t, 0.0, p_persp), 0.0);
    vec2 toP = pz_cube_xskew(p / vec2(t, 1.0), mix(pow(t, 2.0), 1.0, p_persp), 1.0);
    if (pz_cube_inBounds(fromP)) {
        return getFromColor(fromP);
    } else if (pz_cube_inBounds(toP)) {
        return getToColor(toP);
    }
    return pz_cube_bgColor(fromP, toP);
#else
    return vec4(0.0);
#endif
}

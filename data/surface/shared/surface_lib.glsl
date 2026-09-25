// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Shared geometry / composite / focus helpers for SURFACE shader packs — the
// decoration-category cousin of the overlay category's data/overlays/shared/
// common.glsl. Pulls in the uniform contract (surface_uniforms.glsl) and layers
// the small idioms every decoration pack was re-deriving inline: the
// rounded-rect SDF, the frame-geometry setup, the AA slab mask, the focus dim,
// and the three composite forms (border band, slab over window, additive
// margin). Colour-space and noise helpers live in the opt-in modules
// surface_color.glsl / surface_noise.glsl.
//
// Runtime-agnostic in its DECLARATIONS: every helper reads only contract
// uniforms, which are global in both the compositor (default-block) and daemon
// (UBO) branches, so every helper here COMPILES on both.
//
// That is not the same as behaving alike in a BUFFER PASS. The compositor hands
// a buffer pass a subset of the contract, so a helper reading a uniform outside
// that subset compiles and then reads whatever the default-block default is,
// which is zero, while the same helper on the daemon reads the real value from
// the UBO. A helper used from a buffer pass therefore needs checking against
// what that pass is actually given; the main pass has the whole contract on
// both runtimes and is unaffected.

#ifndef PLASMAZONES_SURFACE_LIB_GLSL
#define PLASMAZONES_SURFACE_LIB_GLSL

#include <surface_uniforms.glsl>

const float TAU = 6.28318530718;

// Rounded-box signed distance (iq). `p` is box-centred, `b` half-extents, `r`
// the corner radius. Negative inside, positive outside.
float sdRoundedBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// True before a host has wired a real frame rect. The test is "either side
// below one device px", not "exactly zero": a sub-pixel frame is degenerate
// for the same reason and is treated the same. The
// SDF would otherwise collapse to "edge everywhere", so the border and glow
// family test this and pass content through untouched. The backdrop-slab packs
// do NOT: they fall through to a zero-extent frame, whose mask collapses to
// nothing, so the pane simply does not draw.
bool surfaceFrameDegenerate() {
    return uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0;
}

// Frame geometry + signed distance for a fragment at device-px `p`, with corner
// radius `radiusPx` (device px) clamped to half the smaller side. One call
// replaces the centre / half-size / radius-clamp / SDF idiom every decoration
// pack repeated. Always clamps the radius (blur previously did not — a
// pathological radius on a tiny frame is now clamped like every sibling).
struct FrameSDF {
    vec2 center;
    vec2 halfSize;
    float radius;
    float d;
};
FrameSDF frameSdf(vec2 p, float radiusPx) {
    FrameSDF fs;
    fs.halfSize = 0.5 * uSurfaceFrameSize;
    fs.center = uSurfaceFrameTopLeft + fs.halfSize;
    fs.radius = clamp(radiusPx, 0.0, min(fs.halfSize.x, fs.halfSize.y));
    fs.d = sdRoundedBox(p - fs.center, fs.halfSize, fs.radius);
    return fs;
}

// Rounded box with SEPARATE top and bottom corner radii, for a pane that only
// rounds under a title bar (or only at the bottom). `p` is box-centred in the
// TOP-DOWN px space surfacePixel yields on both runtimes, so the upper half is
// y < 0. Within a quadrant the rounded-box distance depends only on that
// quadrant's corner, so picking the radius by half and reusing sdRoundedBox is
// exact (iq's per-corner variant does the same selection).
float sdRoundedBoxSplit(vec2 p, vec2 b, float rTop, float rBottom) {
    return sdRoundedBox(p, b, p.y < 0.0 ? rTop : rBottom);
}

// frameSdf with separate top / bottom radii (device px), each clamped to half
// the smaller side. `radius` reports the TOP radius, which is the one the
// glass lens builds its normal field from.
FrameSDF frameSdfSplit(vec2 p, float topRadiusPx, float bottomRadiusPx) {
    FrameSDF fs;
    fs.halfSize = 0.5 * uSurfaceFrameSize;
    fs.center = uSurfaceFrameTopLeft + fs.halfSize;
    float cap = min(fs.halfSize.x, fs.halfSize.y);
    fs.radius = clamp(topRadiusPx, 0.0, cap);
    fs.d = sdRoundedBoxSplit(p - fs.center, fs.halfSize, fs.radius, clamp(bottomRadiusPx, 0.0, cap));
    return fs;
}

// Slab AA coverage from an SDF distance (±1 px feather). Border packs use a
// tighter ±0.7 band and pass their own width, so this is the slab form only.
//
// The one-arg form is the third-party convenience overload and is retained for
// that reason, the way surfaceSlabOpen's two-arg form below is: no bundled
// pack calls it (they all pass their own feather), but this is an LGPL library
// header and removing it would be a source break whose failure mode is a
// swallowed compile error and a flat grey decoration.
float frameMask(float d) {
    return 1.0 - smoothstep(-1.0, 1.0, d);
}

// Slab AA coverage with a caller-chosen feather (device px, ± around the
// edge). Floored at a hair so a zero feather cannot collapse smoothstep's two
// edges together (undefined in GLSL), the same guard standardBorderBand uses.
float frameMask(float d, float aa) {
    float feather = max(aa, 1e-3);
    return 1.0 - smoothstep(-feather, feather, d);
}

// Focus dim: `lo` when unfocused, ramping to 1.0 focused, cross-faded on the
// contract's uSurfaceFocused. The unfocused floor is the caller's (packs pick
// 0.30 / 0.55 / 0.65 deliberately).
float focusDim(float lo) {
    return mix(lo, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));
}

// Border-band composite: lay a STRAIGHT-alpha `col` band over `tex`, over
// transparency. The doc used to say premultiplied, and the body is what shows
// it is not: it computes the coverage and then multiplies col.rgb by it, which
// would double-apply the alpha on a premultiplied input.
//
// `edge` and `insideMask` are coverages in [0,1] and the product is clamped,
// because above 1 the `1 - ba` term goes NEGATIVE and the composite starts
// subtracting the content it is supposed to cover. marginComposite one helper
// down was hardened for exactly this and says so; this one stated no domain at
// all, and a third-party pack passing a raw unclamped mask is legal input.
vec4 borderComposite(vec4 tex, vec4 col, float edge, float insideMask) {
    float ba = clamp(edge * insideMask * col.a, 0.0, 1.0);
    vec4 contentPx = tex * (1.0 - edge);
    return vec4(col.rgb * ba, ba) + contentPx * (1.0 - ba);
}

// WINDOW-over-slab composite: the (already opacity-dimmed) window over `pane`.
// The name and the old doc both said pane over window; the body is
// `window + pane * (1 - window.a)`, which is the window on top.
vec4 slabComposite(vec4 window, vec4 pane) {
    return window + pane * (1.0 - window.a);
}

// Additive outer-margin composite (glow / shadow halo `col` at coverage `a`
// over `base`). Coverage is bounded by the FREE alpha so the premultiplied
// invariant (rgb <= a) survives strength > 1: haloFalloff can hand in up to
// strength * (1 - baseAlpha), and clamping only the alpha sum while adding
// col * a to rgb unclamped produced a clipped oversaturated ring where a
// bright halo overlapped a partially transparent base (a KWin decoration
// shadow texel). With ca so bounded the alpha is an exact sum and needs no
// clamp; at strength <= 1 the maths is unchanged.
vec4 marginComposite(vec4 base, vec3 col, float a) {
    // Bounded BELOW as well as above. min() alone let a negative coverage
    // through, and a negative ca subtracts from both rgb and alpha, so a base
    // that was already near zero comes out with NEGATIVE alpha and every later
    // composite in the chain inherits it.
    float ca = clamp(a, 0.0, 1.0 - clamp(base.a, 0.0, 1.0));
    return vec4(base.rgb + col * ca, base.a + ca);
}

// The border family's shared band assembly: the OUTER-radius rounded-rect SDF
// (content radius + border width, both logical px scaled to device px by
// uSurfaceScale), the content-clip mask, and the band edge. `p` is the
// device-px fragment (surfacePixel); `borderWidth` / `cornerRadius` are the
// pack's logical-px params (pack macros the shared code can't name, so they are
// passed in). Packs whose band geometry differs (border-double's three-width
// stack) build their own.
//
// The `aa` feather is the SDF edge softness in DEVICE px (kept unscaled so the
// anti-alias width stays ~constant across output scales). The historical
// family value is 0.7 px — a soft, sub-pixel band. The three-arg form keeps
// that default so every existing caller (window border included) is unchanged;
// the four-arg form lets a pack expose it as a parameter and pass a smaller
// value (~0.5) for a crisper, more grid-hinted 1px hairline.
struct BorderBand {
    FrameSDF fs;
    float insideMask;
    float edge;
};
BorderBand standardBorderBand(vec2 p, float borderWidth, float cornerRadius, float aa) {
    // A zero feather makes both smoothstep() edges equal, which is undefined in
    // GLSL (NaN / garbage on the boundary fragment). Floor it at a hair so an
    // aggressively crisp (or hand-edited) value degrades to a near-hard edge
    // rather than misrendering.
    float feather = max(aa, 1e-3);
    // A width of ZERO is the declared minimum on eight controls across seven
    // packs, and it has to mean NO LINE. Six of those eight come through this
    // helper; border-double builds its own band from frameSdf and is not one of
    // them. For the six, without this guard it does not mean no line: width
    // collapses to 0, the edge term becomes smoothstep(-feather, +feather, d),
    // and that paints a band about two feathers wide straddling the frame edge
    // at up to a quarter of the colour's alpha. The user turns the border off
    // and still sees one. Same guard, same reason, as softBorder in the overlay
    // family's common.glsl.
    if (borderWidth <= 0.0) {
        BorderBand off;
        off.fs = frameSdf(p, cornerRadius * uSurfaceScale);
        off.insideMask = 1.0 - smoothstep(-feather, feather, off.fs.d);
        off.edge = 0.0;
        return off;
    }
    // Bound the band to most of the frame's half extent, the way frameSdf
    // already bounds the radius. Inside the frame the SDF never falls below
    // that half extent, so a wider band would leave every interior fragment on
    // the band side of the smoothstep and paint the whole surface as border.
    // Stopping just short of it keeps a sliver of content visible on a very
    // small window. Same fraction the glass pack uses for its edge.
    vec2 halfSize = 0.5 * uSurfaceFrameSize;
    float width = min(borderWidth * uSurfaceScale, max(0.9 * min(halfSize.x, halfSize.y), 0.1));
    // Radius derives from the CLAMPED width, so the content corner still ends
    // at the requested cornerRadius rather than drifting in the clamped case.
    BorderBand b;
    b.fs = frameSdf(p, cornerRadius * uSurfaceScale + width);
    b.insideMask = 1.0 - smoothstep(-feather, feather, b.fs.d);
    b.edge = smoothstep(-width - feather, -width + feather, b.fs.d);
    return b;
}
BorderBand standardBorderBand(vec2 p, float borderWidth, float cornerRadius) {
    return standardBorderBand(p, borderWidth, cornerRadius, 0.7);
}

// Backdrop-slab family shared open (blur / duotone / frosted-glass / glass /
// mosaic / phosphor-glass / rain-glass / rippled-glass): the raw window
// sample, the device-px fragment, the frame SDF at the pack's corner radius,
// and the AA slab mask — the four lines every backdrop-slab pack repeats
// before its pack-specific pane. Content dimming is a per-pack concern now: a
// pack that wants a fadeable window sample declares its own `contentOpacity`
// parameter and multiplies `window` itself, so its knob lives in its param
// editor instead of riding the retired SetOpacity rule feed. All eight of the
// packs above declare it, so it reads as part of the family rather than a
// frost/glass peculiarity.
// `cornerRadiusPx` is the pack's p_cornerRadius already scaled to device px.
struct SurfaceSlab {
    vec4 window;
    vec2 px;
    FrameSDF fs;
    float mask;
};

// The four-arg form is what the bundled slab packs call: separate top / bottom
// radii (a pack's `roundBottomCorners` switch hands 0 for the bottom) and the
// edge feather in device px (a pack's `edgeSoftness`). The two-arg form keeps
// the original symmetric, 1 px-feather behaviour for third-party packs.
SurfaceSlab surfaceSlabOpen(vec2 uv, float topRadiusPx, float bottomRadiusPx, float aa) {
    SurfaceSlab s;
    s.px = surfacePixel(uv);
    s.fs = frameSdfSplit(s.px, topRadiusPx, bottomRadiusPx);
    s.mask = frameMask(s.fs.d, aa);
    // The window sample is clipped to the same rounded frame as the pane. The
    // capture is square-cornered (the pack owns the corner radius), so an
    // unmasked window pokes its square corners past the rounded slab at any
    // contentOpacity below 1 — and at 1.0 the radius parameter does nothing.
    // Premultiplied, so one multiply rounds both colour and coverage.
    s.window = surfaceTexel(uv) * s.mask;
    return s;
}
SurfaceSlab surfaceSlabOpen(vec2 uv, float cornerRadiusPx) {
    return surfaceSlabOpen(uv, cornerRadiusPx, cornerRadiusPx, 1.0);
}

// The backdrop-slab family's shared no-backdrop fallback (any host where
// uHasBackdrop is 0): a faint premultiplied tint slab clipped to the slab
// `mask`. Only packs that use exactly this profile (blur / glass /
// rippled-glass) call it; siblings with a different fallback keep theirs.
//
// The alpha carries a visibility FLOOR, which is the whole point of the
// fallback. Its callers' headers say it exists "so previews still communicate
// the pack's shape", and at 0.35 * tintStrength that was false at the one
// setting where it matters most: tintStrength declares a minimum of 0 on all
// three packs, where the slab drew NOTHING and the shape was not communicated
// at all. Even at their shipped defaults (0.15, 0.1, 0.1) it reached only 5.25%
// and 3.5%. The floor applies ONLY to this degraded no-backdrop path; on the
// real path tintStrength 0 still means no tint, because that is a statement
// about the tint rather than about whether the pane is visible.
vec4 faintTintSlab(vec3 tint, float tintStrength, float mask) {
    // Clamped, because nothing bounds the inputs. The bundled packs declare
    // tintStrength at most 1, where 0.35 * ts can never exceed 1, but a
    // third-party pack may declare any range, and above about 2.86 the alpha
    // passes 1 and the result stops satisfying rgb <= a — the premultiplied
    // invariant every consumer of this library relies on. The mask multiplies
    // AFTER the floor so the slab still respects the rounded corners.
    float a = clamp(max(0.35 * tintStrength, 0.1) * mask, 0.0, 1.0);
    return vec4(clamp(tint, 0.0, 1.0) * a, a);
}

// Glow/shadow outer-margin falloff (the ~12 lines glow and shadow shared): the
// exp(-4t²) reach profile from SDF distance `d`, feathered to zero just inside
// the texture edge so a thin capture margin fades out instead of clipping in a
// hard rectangle, held to where the surface is not opaque (1 - baseAlpha) AND to
// within one reach inside the frame, and scaled by `strength` and the focus dim
// at floor `focusFloor`. `edgePx` is the
// REAL (undisplaced) fragment position for the edge feather — the shadow pack
// evaluates `d` against a displaced frame but feathers on the true position.
float haloFalloff(float d, float reach, vec2 edgePx, float baseAlpha, float strength, float focusFloor) {
    // reach is caller-supplied and a zero would make this inf, then NaN through
    // exp(), and a NaN propagates through the whole composite rather than
    // showing up as one bad pixel. Both in-tree callers (glow, shadow) floor it
    // at 1.0, so this changes no output today; it is here so that a caller which
    // stops flooring cannot poison the frame.
    //
    // Clamping d at 0 makes t zero for every fragment INSIDE the frame, so the
    // profile there is exp(0) = 1, the full value rather than a falloff. That is
    // deliberate and stays: the shadow pack evaluates d against a DISPLACED frame,
    // so part of its band legitimately lies inside the real one, and a pack whose
    // halo radius is smaller than the border pack's needs the transparent CORNER
    // SLIVER just inside the edge.
    //
    // But it left (1 - baseAlpha) as the ONLY confinement, which is confinement to
    // TRANSPARENCY and not to the margin, so a natively translucent client wore the
    // halo across its whole body. Two earlier candidates were rejected for costing
    // one of the two cases above. The depth gate below costs neither.
    float r = max(reach, 1e-3);
    float t = max(d, 0.0) / r;
    float halo = exp(-4.0 * t * t);
    // DEPTH GATE. A halo has no business reaching further INSIDE the frame than
    // its reach carries it outside, and both cases the clamp above protects sit
    // within one reach of the edge: the corner sliver is just inside it, and the
    // shadow's displaced band is within its own reach by construction. So hold the
    // profile at full value down to one reach inside and fade it out by two, which
    // zeroes only the deep interior. For an opaque window baseAlpha is 1 and the
    // halo was already 0, so this changes nothing there.
    halo *= smoothstep(-2.0 * r, -r, d);
    float edgeDist = min(min(edgePx.x, edgePx.y), min(uSurfaceSize.x - edgePx.x, uSurfaceSize.y - edgePx.y));
    // Floored so edge0 != edge1: smoothstep is undefined when they are equal, and
    // a zero reach collapsed them. With this, a zero reach is wholly safe rather
    // than half-guarded.
    float feather = max(min(0.35 * reach, 12.0 * max(uSurfaceScale, 0.001)), 1e-3);
    halo *= smoothstep(0.0, feather, edgeDist);
    halo *= (1.0 - baseAlpha);
    halo *= strength * focusDim(focusFloor);
    return halo;
}

// Frame-normalised UV for a device-px fragment. In [0,1] only for a fragment
// INSIDE the frame rect: the padded canvas extends beyond it, so a fragment in
// the outer margin comes back negative or past 1, which is what the margin
// packs rely on.
vec2 frameUv(vec2 px) {
    return (px - uSurfaceFrameTopLeft) / max(uSurfaceFrameSize, vec2(1.0));
}

// Inverse of surfacePixel: a top-down device-px POSITION back to the canvas uv
// that samples it, with the same per-runtime Y flip.
vec2 surfaceUvFromPixel(vec2 px) {
    vec2 n = px / max(uSurfaceSize, vec2(1.0));
#ifdef PLASMAZONES_KWIN
    return vec2(n.x, 1.0 - n.y);
#else
    return n;
#endif
}

// A canvas uv that a refraction pushed past the FRAME rect, mirrored back
// inside it (the way Better Blur's "texture repeat" mode reflects the blur at
// the pane edge, so a strong bend shows the pane's own interior folding over
// rather than a stretched edge pixel). Mirrors in frame-normalized space, so
// the fold happens at the pane's edge and not at the padded canvas's.
vec2 frameMirrorUv(vec2 uv) {
    vec2 f = frameUv(surfacePixel(uv));
    vec2 t = mod(f, 2.0);
    f = mix(t, 2.0 - t, step(1.0, t));
    return surfaceUvFromPixel(uSurfaceFrameTopLeft + f * uSurfaceFrameSize);
}

// Where a bent sample LANDS: folded back inside the frame when @p mirror, else
// clamped to the canvas.
//
// The two-way choice was written out three times, once per refracting pack
// (glass, rippled-glass, rain-glass). frameMirrorUv above was already shared,
// but the POLICY around it was not, and the policy is the part that has to
// agree: a pack that clamped where its siblings mirror shows a stretched edge
// pixel where they show the pane folding over, on the same user setting.
//
// Takes the flag rather than reading a parameter, because `p_edgeMirror` is a
// per-pack generated name that a shared header cannot see. The packs keep their
// own one-line wrappers for readability and pass the flag through.
vec2 surfaceBendUv(vec2 uv, bool mirror) {
    return mirror ? frameMirrorUv(uv) : clamp(uv, 0.0, 1.0);
}

// px-space (top-down) vector -> canvas UV offset, used for backdrop
// refraction offsets. The flip is per-runtime for the same reason
// surfacePixel's is: px space is top-down on both hosts, but the compositor
// reaches it from a Y-up vTexCoord while the daemon's is already Y-down. A
// vector converted with the wrong sign refracts the backdrop the opposite way
// vertically, which only became reachable on the daemon once a host could
// raise uHasBackdrop by binding a stand-in backdrop.
vec2 pxToUv(vec2 v) {
#ifdef PLASMAZONES_KWIN
    return vec2(v.x, -v.y) / max(uSurfaceSize, vec2(1.0));
#else
    return v / max(uSurfaceSize, vec2(1.0));
#endif
}

// Normalised perimeter angle in [-0.5, 0.5] around the frame centre,
// aspect-corrected by dividing through the half-extents.
//
// That correction equalises the dash COUNT per side, not the dash SIZE, and
// the doc used to claim the opposite. Normalising each axis independently
// gives a wide frame the same number of dashes along its long side as its
// short one, so they are correspondingly longer there. Uniform dashes would
// need arc length, which this deliberately does not compute.
float framePerimeter(vec2 p, vec2 center, vec2 halfSize) {
    vec2 rel = (p - center) / max(halfSize, vec2(1.0));
    // atan(0, 0) is undefined in GLSL, and the exact frame centre reaches it on
    // any frame whose centre lands on a fragment centre. Returning the start of
    // the sweep is the only answer continuous with its neighbourhood, since
    // every direction meets there.
    if (dot(rel, rel) < 1e-8) {
        return 0.0;
    }
    return atan(rel.y, rel.x) / TAU;
}

#endif // PLASMAZONES_SURFACE_LIB_GLSL

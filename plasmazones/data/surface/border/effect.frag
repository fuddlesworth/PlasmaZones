// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Border surface shader — rounded corners + window border, the first surface
// pack. Width, corner radius and colours are this pack's own PARAMETERS (not a
// separate host-defined "decoration appearance"): p_borderWidth / p_cornerRadius
// (logical px, scaled to device px by uSurfaceScale) and p_activeColor /
// p_inactiveColor, mixed on the contract's uSurfaceFocused so the focused vs
// unfocused colour is the shader's job.
//
// THREE of this pack's declared parameters are consumed HOST-SIDE and are
// never read here, which is why the shader only ever reads the colour params.
// p_useThemeNeutral fills active/inactive from a neutral line lerped between
// the theme background and foreground, with p_frameContrast choosing how far
// along that line. p_useSystemAccent fills them from the system accent
// instead. useThemeNeutral WINS: the resolver tests it first and the accent
// branch is its else, so a pack with both set never sees the accent.
//
// One analytic rounded-rect SDF over the content/frame rect both clips the
// content to the inner rounded rect and lays the border band over the
// background, so a translucent border blends with what is behind the surface.
//
// Written against the pSurface entry scaffold: this pack defines only
// `vec4 pSurface(vec2 uv)` and the harness generates the main() + the
// #version / #include <surface_lib.glsl> / layout prologue (see
// SurfaceShaderRegistry::surfaceEntryPrologue).

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    // Identity-decoration state: before a host wires real geometry the frame
    // rect is degenerate (uSurfaceFrameSize == 0). The SDF below would collapse
    // to "edge everywhere" and paint a border over the whole surface, so pass
    // the captured content through untouched until a real frame arrives.
    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's rounded-rect SDF (outer radius = content radius +
    // width, except at a zero end, which stays square), content clip
    // and band edge from this pack's logical-px width and corner radius. The
    // bottom corners carry their own radius so this pack traces the same
    // outline as a backdrop pack squared off against a panel or a screen edge.
    float bottomRadius = surfaceBottomRadius(p_cornerRadius, p_roundBottomCorners);
    BorderBand bb =
        standardBorderBandSplit(surfacePixel(uv), p_borderWidth, p_cornerRadius, bottomRadius, p_edgeSoftness);

    // Focus-mixed border colour (the shader picks active vs inactive).
    vec4 outlineColor = mix(p_inactiveColor, p_activeColor, clamp(uSurfaceFocused, 0.0, 1.0));

    // Clip content to the inner rounded rect; lay the band over transparency,
    // premultiplied. At width <= 0 (no border in the chain's params) the helper
    // returns edge = 0, and borderComposite clips content by (1 - edge), so the
    // capture passes through UNCHANGED — square corners and no band, not rounded
    // corners with no band. Rounding without a band would need a composite arm
    // that this signature cannot express, so it is an API question rather than
    // something to patch here.
    return borderComposite(tex, outlineColor, bb.edge, bb.insideMask);
}

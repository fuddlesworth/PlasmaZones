// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_anchor_uniforms.cpp
 * @brief Pure-geometry tests for ShaderInternal::computeAnchorUniforms().
 *
 * The kwin-effect feeds animation shaders three anchor uniforms —
 * iResolution, iAnchorSize, iAnchorPosInFbo — that tell a shader where the
 * captured window sits inside its render target. The helper takes
 * `(anchor, texture)` and returns `{texture.size, anchor.size, anchor -
 * texture.topLeft}`; callers choose what the `texture` rect is per extent
 * mode (anchor-extent: the redirected FBO, frame-sized on the daemon and
 * expanded-sized on KWin; surface-extent: the output).
 *
 * This pins the math so a regression in the helper can't silently
 * mis-place a surface-extent effect or break the anchor-extent identity
 * case that every other animation depends on.
 */

#include <QTest>
#include <QVector2D>
#include <QVector4D>

#include <plasmazoneseffect/shader_internal.h>

using PlasmaZones::ShaderInternal::AnchorUniforms;
using PlasmaZones::ShaderInternal::computeAnchorUniforms;
using PlasmaZones::ShaderInternal::computeTextureSubRect;

class TestAnchorUniforms : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // Anchor extent on the daemon: texture == anchor (FBO covers the
    // window 1:1), so iResolution == iAnchorSize and the anchor sits at
    // the origin.
    void testAnchorExtentIdentityWhenTextureEqualsAnchor()
    {
        const QRectF anchor(100.0, 200.0, 800.0, 600.0);
        const AnchorUniforms u = computeAnchorUniforms(anchor, /*texture=*/anchor);
        QCOMPARE(u.resolution, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(0.0f, 0.0f));
    }

    // Anchor extent on KWin: the redirected FBO covers the EXPANDED rect
    // (frame + shadow), so iResolution grows to the expanded size,
    // iAnchorSize stays the frame size, and iAnchorPosInFbo is the
    // shadow inset. Shaders fold these back via anchorRemap so their
    // [0,1] UV spans the frame, not the shadow edge.
    void testAnchorExtentExpandedFboHasShadowInset()
    {
        const QRectF frame(1604.0, 894.0, 1588.0, 846.0);
        const QRectF expanded(1590.0, 880.0, 1616.0, 874.0); // 14px shadow each side, top, bottom
        const AnchorUniforms u = computeAnchorUniforms(frame, expanded);
        QCOMPARE(u.resolution, QVector2D(1616.0f, 874.0f));
        QCOMPARE(u.anchorSize, QVector2D(1588.0f, 846.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(14.0f, 14.0f));
    }

    // Surface extent: iResolution grows to the output, iAnchorSize stays
    // the window size, and iAnchorPosInFbo is the window's offset within
    // the output (output at the global origin here).
    void testSurfaceExtentPlacesWindowInOutput()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.0, 200.0, 800.0, 600.0), QRectF(0.0, 0.0, 1920.0, 1080.0));
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.0f, 200.0f));
    }

    // Surface extent on a non-primary output: anchorPosInFbo is relative
    // to the output's own top-left, not the global compositor origin.
    void testSurfaceExtentAnchorRelativeToOutput()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(2000.0, 150.0, 400.0, 300.0), QRectF(1920.0, 0.0, 2560.0, 1440.0));
        QCOMPARE(u.resolution, QVector2D(2560.0f, 1440.0f));
        QCOMPARE(u.anchorSize, QVector2D(400.0f, 300.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(80.0f, 150.0f));
    }

    // A window flush against its output's top-left collapses surface
    // extent's anchorPosInFbo to (0, 0) — the same origin as anchor
    // extent — while iResolution still reports the full output.
    void testSurfaceExtentWindowAtOutputOrigin()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(1920.0, 0.0, 1280.0, 720.0), QRectF(1920.0, 0.0, 2560.0, 1440.0));
        QCOMPARE(u.resolution, QVector2D(2560.0f, 1440.0f));
        QCOMPARE(u.anchorSize, QVector2D(1280.0f, 720.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(0.0f, 0.0f));
    }

    // Surface extent with the window straddling its output's top-left
    // corner: anchorPosInFbo goes negative. The helper does not clamp;
    // a surface-extent shader's anchorRemap is expected to cope with a
    // window that extends past the output edge.
    void testSurfaceExtentNegativeAnchorOffset()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(-30.0, -20.0, 800.0, 600.0), QRectF(0.0, 0.0, 1920.0, 1080.0));
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(-30.0f, -20.0f));
    }

    // Degenerate zero-size window: anchorSize clamps up to 1.0 to keep
    // shader divisions safe (anchorRemap divides by iAnchorSize). The
    // anchor offset still flows through unmodified so callers see the
    // original placement.
    void testDegenerateZeroSizeWindowClampsAnchorSize()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.0, 200.0, 0.0, 0.0), QRectF(0.0, 0.0, 1920.0, 1080.0));
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(1.0f, 1.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.0f, 200.0f));
    }

    // Fractional geometry: fractional-scale outputs hand frameGeometry
    // sub-pixel coordinates. The float conversions and the offset
    // subtraction stay exact for representable fractions. Values chosen
    // (0.5 / 0.25 / 0.75) are exact in IEEE-754 single precision so the
    // strict QCOMPARE equality is intentional; non-power-of-two fractions
    // (e.g. 100.1) would need qFuzzyCompare instead.
    void testSurfaceExtentFractionalGeometry()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.5, 200.25, 800.5, 600.75), QRectF(0.0, 0.0, 1920.0, 1080.0));
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.5f, 600.75f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.5f, 200.25f));
    }

    // computeTextureSubRect — iAnchorRectInTexture. Anchor-extent
    // transitions pass (expanded, expanded): inner == outer collapses to
    // the (0, 0, 1, 1) identity surfaceColor() treats as a passthrough.
    void testTextureSubRectIdentityWhenInnerEqualsOuter()
    {
        const QRectF rect(100.0, 200.0, 800.0, 600.0);
        QCOMPARE(computeTextureSubRect(rect, /*outer=*/rect), QVector4D(0.0f, 0.0f, 1.0f, 1.0f));
    }

    // Surface-extent transitions pass (frame, expanded): the frame's UV
    // sub-rect within the shadow-padded redirected FBO. surfaceColor()
    // folds this in so anchor-[0,1] samples land on the frame's region
    // of uTexture0 instead of stretching frame+shadow across the frame.
    void testTextureSubRectFrameWithinExpandedFbo()
    {
        const QRectF frame(140.0, 120.0, 1600.0, 900.0);
        const QRectF expanded(100.0, 100.0, 2000.0, 1000.0); // 40px left, 20px top shadow inset
        QCOMPARE(computeTextureSubRect(frame, expanded), QVector4D(0.02f, 0.02f, 0.8f, 0.9f));
    }

    // A window with no decoration or shadow extents reports an expanded
    // rect equal to its frame, so the sub-rect is again the identity.
    void testTextureSubRectNoShadowIsIdentity()
    {
        const QRectF frame(0.0, 0.0, 1280.0, 720.0);
        QCOMPARE(computeTextureSubRect(frame, /*outer=*/frame), QVector4D(0.0f, 0.0f, 1.0f, 1.0f));
    }

    // Degenerate zero-size outer: width/height clamp to 1.0 so the
    // divisions stay finite rather than producing inf/NaN.
    //
    // The original symmetric inner==outer==zero case happened to pass
    // even without the clamp (0/0 -> NaN, but the test compared 0==0 as
    // a coincidence of both numerator and denominator being zero).
    // Asymmetric case below — non-zero inner, zero outer — is what
    // actually exercises the `qMax(outer.width(), 1.0)` clamp: without
    // it we get a divide-by-zero; with it the result is finite and
    // equals `inner.width / 1.0 = inner.width` straight through.
    void testTextureSubRectClampsDegenerateOuterDenominator()
    {
        const QVector4D r = computeTextureSubRect(QRectF(50.0, 50.0, 800.0, 600.0), QRectF(50.0, 50.0, 0.0, 0.0));
        QCOMPARE(r, QVector4D(0.0f, 0.0f, 800.0f, 600.0f));
    }

    // Pin the symmetric clamp on `computeAnchorUniforms` for a
    // zero-size TEXTURE (degenerate render-target — runtime race during
    // attach). resolution clamps to (1.0, 1.0) so anchorRemap's division
    // by `iResolution` stays finite. anchorSize is preserved at its
    // unclamped value (anchor=window can be normal even if texture is
    // mid-resize). The companion `testDegenerateZeroSizeWindowClampsAnchorSize`
    // pins the window-clamp branch; this one pins the texture-clamp branch.
    void testDegenerateZeroSizeTextureClampsResolution()
    {
        const AnchorUniforms u = computeAnchorUniforms(QRectF(100.0, 200.0, 800.0, 600.0), QRectF(0.0, 0.0, 0.0, 0.0));
        QCOMPARE(u.resolution, QVector2D(1.0f, 1.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.0f, 200.0f));
    }

    // Anchor wider/taller than texture (window straddles output edge,
    // or anchor-extent transition with anchor exceeding texture for
    // some reason). The helper does NOT clamp width/height ratio above
    // 1.0 — surface-extent shaders are responsible for handling
    // `iAnchorRectInTexture` components > 1. Pinning the no-clamp
    // contract here so a future "defensive" clamp wouldn't silently
    // clip windows.
    void testTextureSubRectInnerExceedsOuter()
    {
        const QVector4D r = computeTextureSubRect(QRectF(0.0, 0.0, 2000.0, 1500.0), QRectF(0.0, 0.0, 1000.0, 1000.0));
        QCOMPARE(r, QVector4D(0.0f, 0.0f, 2.0f, 1.5f));
    }
};

QTEST_APPLESS_MAIN(TestAnchorUniforms)
#include "test_anchor_uniforms.moc"

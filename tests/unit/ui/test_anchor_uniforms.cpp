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

#include <plasmazoneseffect/shader_internal.h>

using PlasmaZones::ShaderInternal::AnchorUniforms;
using PlasmaZones::ShaderInternal::computeAnchorUniforms;

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
    // subtraction stay exact for representable fractions.
    void testSurfaceExtentFractionalGeometry()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.5, 200.25, 800.5, 600.75), QRectF(0.0, 0.0, 1920.0, 1080.0));
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.5f, 600.75f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.5f, 200.25f));
    }
};

QTEST_APPLESS_MAIN(TestAnchorUniforms)
#include "test_anchor_uniforms.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_anchor_uniforms.cpp
 * @brief Pure-geometry tests for ShaderInternal::computeAnchorUniforms().
 *
 * The kwin-effect feeds animation shaders three anchor uniforms —
 * iResolution, iAnchorSize, iAnchorPosInFbo — that tell a shader where the
 * captured window sits inside its render target. Anchor-extent transitions
 * render 1:1 over the window; surface-extent transitions (metadata
 * `fboExtent: "surface"`: broken-glass, fly-in, morph) render over the
 * whole output with the window placed at a sub-region.
 *
 * This pins the branch math so a regression in the helper can't silently
 * mis-place a surface-extent effect (or break the anchor-extent identity
 * case that every other animation depends on).
 */

#include <QTest>
#include <QVector2D>

#include "../../../kwin-effect/plasmazoneseffect/shader_internal.h"

using PlasmaZones::ShaderInternal::AnchorUniforms;
using PlasmaZones::ShaderInternal::computeAnchorUniforms;

class TestAnchorUniforms : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // Anchor extent: the render target covers the window 1:1.
    // iResolution == iAnchorSize == window size, anchor at the origin.
    // The output geometry is ignored in this mode.
    void testAnchorExtentIsWindowIdentity()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.0, 200.0, 800.0, 600.0), QRectF(0.0, 0.0, 1920.0, 1080.0),
                                  /*surfaceExtent=*/false);
        QCOMPARE(u.resolution, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(0.0f, 0.0f));
    }

    // Anchor extent ignores the output even when the window sits away
    // from the output origin — anchorPosInFbo stays (0, 0).
    void testAnchorExtentIgnoresOutputOrigin()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(2000.0, 150.0, 400.0, 300.0), QRectF(1920.0, 0.0, 2560.0, 1440.0),
                                  /*surfaceExtent=*/false);
        QCOMPARE(u.resolution, QVector2D(400.0f, 300.0f));
        QCOMPARE(u.anchorSize, QVector2D(400.0f, 300.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(0.0f, 0.0f));
    }

    // Surface extent: iResolution grows to the output, iAnchorSize stays
    // the window size, and iAnchorPosInFbo is the window's offset within
    // the output (output at the global origin here).
    void testSurfaceExtentPlacesWindowInOutput()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(100.0, 200.0, 800.0, 600.0), QRectF(0.0, 0.0, 1920.0, 1080.0),
                                  /*surfaceExtent=*/true);
        QCOMPARE(u.resolution, QVector2D(1920.0f, 1080.0f));
        QCOMPARE(u.anchorSize, QVector2D(800.0f, 600.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(100.0f, 200.0f));
    }

    // Surface extent on a non-primary output: anchorPosInFbo is relative
    // to the output's own top-left, not the global compositor origin.
    void testSurfaceExtentAnchorRelativeToOutput()
    {
        const AnchorUniforms u =
            computeAnchorUniforms(QRectF(2000.0, 150.0, 400.0, 300.0), QRectF(1920.0, 0.0, 2560.0, 1440.0),
                                  /*surfaceExtent=*/true);
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
            computeAnchorUniforms(QRectF(1920.0, 0.0, 1280.0, 720.0), QRectF(1920.0, 0.0, 2560.0, 1440.0),
                                  /*surfaceExtent=*/true);
        QCOMPARE(u.resolution, QVector2D(2560.0f, 1440.0f));
        QCOMPARE(u.anchorSize, QVector2D(1280.0f, 720.0f));
        QCOMPARE(u.anchorPosInFbo, QVector2D(0.0f, 0.0f));
    }
};

QTEST_APPLESS_MAIN(TestAnchorUniforms)
#include "test_anchor_uniforms.moc"

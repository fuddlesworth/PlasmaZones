// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pure half of TransitionPass (transitionpasspure.h): the on-screen
// format to alpha-capable capture format mapping the strip pass allocates
// with. Header-only plain GL enums, included straight from the effect tree
// like test_strip_motion_sampler. The mapping decides whether a 10-bit or
// HDR output keeps its precision through the pass and whether a capture can
// carry the coverage alpha the pass relies on, so the table is pinned here.

#include "transitions/transitionpasspure.h"

#include <QTest>

using PlasmaZones::TransitionPass::alphaCaptureFormatForInternalFormat;

class TestTransitionPassPure : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void wideAndFloatTargetsKeepPrecision_data()
    {
        QTest::addColumn<GLenum>("target");
        QTest::newRow("rgba16f") << GLenum(GL_RGBA16F);
        QTest::newRow("rgb16f") << GLenum(GL_RGB16F);
        QTest::newRow("rgba32f") << GLenum(GL_RGBA32F);
        QTest::newRow("rgb32f") << GLenum(GL_RGB32F);
        QTest::newRow("r11f_g11f_b10f") << GLenum(GL_R11F_G11F_B10F);
        // 10-bit SDR: the 2-bit alpha cannot carry coverage, so the wide
        // format is promoted rather than reused.
        QTest::newRow("rgb10_a2") << GLenum(GL_RGB10_A2);
        QTest::newRow("rgba16") << GLenum(GL_RGBA16);
        QTest::newRow("rgb16") << GLenum(GL_RGB16);
    }

    void wideAndFloatTargetsKeepPrecision()
    {
        QFETCH(GLenum, target);
        QCOMPARE(alphaCaptureFormatForInternalFormat(target), GLenum(GL_RGBA16F));
    }

    void eightBitAndUnknownTargetsGetRgba8_data()
    {
        QTest::addColumn<GLenum>("target");
        QTest::newRow("rgba8") << GLenum(GL_RGBA8);
        // KWin reports an XRGB8888 buffer as GL_RGBA8 with no alpha bits;
        // GL_RGB8 never appears but must still map somewhere sane.
        QTest::newRow("rgb8") << GLenum(GL_RGB8);
        QTest::newRow("srgb8_alpha8") << GLenum(GL_SRGB8_ALPHA8);
        QTest::newRow("unknown") << GLenum(0);
    }

    void eightBitAndUnknownTargetsGetRgba8()
    {
        QFETCH(GLenum, target);
        QCOMPARE(alphaCaptureFormatForInternalFormat(target), GLenum(GL_RGBA8));
    }

    void quadVertexSourceDeclaresTheContract()
    {
        // The strip and desktop packs' generated main reads vTexCoord at
        // location 0 and the passes upload the projection under this name.
        const QString src = QString::fromLatin1(PlasmaZones::TransitionPass::kOutputQuadVertexSource);
        QVERIFY(src.startsWith(QStringLiteral("#version 450")));
        QVERIFY(src.contains(QStringLiteral("uniform mat4 modelViewProjectionMatrix;")));
        QVERIFY(src.contains(QStringLiteral("layout(location = 0) out vec2 vTexCoord;")));
    }
};

QTEST_APPLESS_MAIN(TestTransitionPassPure)
#include "test_transition_pass_pure.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pins the std140 layout of AnimationUniformExtension — the trailing UBO
// region animation shaders read as iSurfaceScreenPos / iAnchorSize /
// iAnchorPosInFbo / iAnchorRectInTexture. The GLSL UBO declaration in
// data/animations/shared/animation_uniforms.glsl hard-codes those
// offsets (672 / 688 / 696 / 704); any drift between the C++ struct and
// the GLSL block makes every animation shader read garbage. The
// extension is render-thread write-only and has no getters by design,
// so the test inspects the byte image write() produces.

#include <PhosphorAnimation/AnimationUniformExtension.h>
#include <PhosphorShaders/BaseUniforms.h>

#include <QPointF>
#include <QSizeF>
#include <QTest>
#include <QVector2D>
#include <QVector4D>

#include <array>
#include <cstring>

using PhosphorAnimation::AnimationUniformExtension;

namespace {
/// Pull one float out of the write() byte image at a byte offset.
float floatAt(const std::array<char, 672>& buf, int byteOffset)
{
    float v = 0.0f;
    std::memcpy(&v, buf.data() + byteOffset, sizeof(float));
    return v;
}
} // namespace

class TestAnimationUniformExtension : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // The extension appends exactly 672 bytes after BaseUniforms (anchor
    // fields, the transition-class tail, the icon rect and the two 16-slot
    // move arrays at std140 vec2 stride 16), landing the animation UBO
    // total at 1344. animation_uniforms.glsl's UBO branch declares the
    // matching trailing fields — a mismatch desyncs every animation
    // shader's UBO upload.
    void testExtensionSizeAndUboTotal()
    {
        AnimationUniformExtension ext;
        QCOMPARE(ext.extensionSize(), 672);
        QCOMPARE(static_cast<int>(sizeof(PhosphorShaders::BaseUniforms)) + ext.extensionSize(), 1344);
    }

    // A freshly constructed extension carries the (0, 0, 1, 1) identity
    // in iAnchorRectInTexture, so surfaceColor() / animation.vert stay a
    // passthrough before SurfaceAnimator's first geometry sync. A zero
    // width/height would collapse every sample to the corner texel and
    // divide the vertex remap by zero.
    void testDefaultAnchorRectIsIdentity()
    {
        AnimationUniformExtension ext;
        std::array<char, 672> buf{};
        ext.write(buf.data(), 0);
        QCOMPARE(floatAt(buf, 32), 0.0f); // x
        QCOMPARE(floatAt(buf, 36), 0.0f); // y
        QCOMPARE(floatAt(buf, 40), 1.0f); // width
        QCOMPARE(floatAt(buf, 44), 1.0f); // height
    }

    // Every setter lands its value at the std140 offset
    // animation_uniforms.glsl's UBO branch declares, expressed here
    // relative to the extension's start (UBO offset minus
    // sizeof(BaseUniforms)):
    //   iSurfaceScreenPos    @ 0  (UBO 672)
    //   iAnchorSize          @ 16 (UBO 688)
    //   iAnchorPosInFbo      @ 24 (UBO 696)
    //   iAnchorRectInTexture @ 32 (UBO 704)
    void testSettersWriteAtDeclaredOffsets()
    {
        AnimationUniformExtension ext;
        ext.setISurfaceScreenPos(QVector4D(11.0f, 22.0f, 33.0f, 44.0f));
        ext.setIAnchorSize(QSizeF(55.0, 66.0));
        ext.setIAnchorPosInFbo(QPointF(77.0, 88.0));
        ext.setIAnchorRectInTexture(QVector4D(0.1f, 0.2f, 0.5f, 0.625f));

        std::array<char, 672> buf{};
        ext.write(buf.data(), 0);

        QCOMPARE(floatAt(buf, 0), 11.0f);
        QCOMPARE(floatAt(buf, 4), 22.0f);
        QCOMPARE(floatAt(buf, 8), 33.0f);
        QCOMPARE(floatAt(buf, 12), 44.0f);

        QCOMPARE(floatAt(buf, 16), 55.0f);
        QCOMPARE(floatAt(buf, 20), 66.0f);

        QCOMPARE(floatAt(buf, 24), 77.0f);
        QCOMPARE(floatAt(buf, 28), 88.0f);

        QCOMPARE(floatAt(buf, 32), 0.1f);
        QCOMPARE(floatAt(buf, 36), 0.2f);
        QCOMPARE(floatAt(buf, 40), 0.5f);
        QCOMPARE(floatAt(buf, 44), 0.625f);
    }

    // The transition-class tail lands at the offsets the GLSL block
    // declares, relative to the extension's start:
    //   iSwitchDelta      @ 48  (UBO 720)
    //   iStripMotion      @ 64  (UBO 736)
    //   iStripRect        @ 80  (UBO 752)
    //   iFromRect         @ 96  (UBO 768)
    //   iToRect           @ 112 (UBO 784)
    //   iStripAxis        @ 128 (UBO 800)
    //   iOldWindowOpacity @ 136 (UBO 808)
    void testTransitionTailOffsets()
    {
        AnimationUniformExtension ext;
        ext.setISwitchDelta(QVector4D(1.0f, 2.0f, 3.0f, 4.0f));
        ext.setIStripMotion(QVector4D(5.0f, 6.0f, 7.0f, 8.0f));
        ext.setIStripRect(QVector4D(9.0f, 10.0f, 11.0f, 12.0f));
        ext.setIFromRect(QVector4D(13.0f, 14.0f, 15.0f, 16.0f));
        ext.setIToRect(QVector4D(17.0f, 18.0f, 19.0f, 20.0f));
        ext.setIStripAxis(QVector2D(21.0f, 22.0f));
        ext.setIOldWindowOpacity(0.75f);

        std::array<char, 672> buf{};
        ext.write(buf.data(), 0);

        QCOMPARE(floatAt(buf, 48), 1.0f);
        QCOMPARE(floatAt(buf, 60), 4.0f);
        QCOMPARE(floatAt(buf, 64), 5.0f);
        QCOMPARE(floatAt(buf, 80), 9.0f);
        QCOMPARE(floatAt(buf, 96), 13.0f);
        QCOMPARE(floatAt(buf, 112), 17.0f);
        QCOMPARE(floatAt(buf, 128), 21.0f);
        QCOMPARE(floatAt(buf, 132), 22.0f);
        QCOMPARE(floatAt(buf, 136), 0.75f);
    }

    // The icon rect and move arrays land at their declared offsets:
    //   iIconRect  @ 144 (UBO 816)
    //   iMoveTrail @ 160 (UBO 832), std140 vec2 stride 16
    //   iMoveMesh  @ 416 (UBO 1088), same stride
    void testIconRectAndMoveArrayOffsets()
    {
        AnimationUniformExtension ext;
        ext.setIIconRect(QVector4D(30.0f, 40.0f, 48.0f, 6.0f));
        ext.setIMoveTrail({QVector2D(1.5f, -2.5f), QVector2D(3.0f, -4.0f)});
        ext.setIMoveMesh({QVector2D(7.0f, 8.0f)});

        std::array<char, 672> buf{};
        ext.write(buf.data(), 0);

        QCOMPARE(floatAt(buf, 144), 30.0f);
        QCOMPARE(floatAt(buf, 156), 6.0f);
        // trail[0] at 160, trail[1] one 16-byte stride later.
        QCOMPARE(floatAt(buf, 160), 1.5f);
        QCOMPARE(floatAt(buf, 164), -2.5f);
        QCOMPARE(floatAt(buf, 176), 3.0f);
        QCOMPARE(floatAt(buf, 180), -4.0f);
        // Unsupplied entries zero-fill.
        QCOMPARE(floatAt(buf, 192), 0.0f);
        QCOMPARE(floatAt(buf, 416), 7.0f);
        QCOMPARE(floatAt(buf, 420), 8.0f);
    }

    // write() honours its `offset` argument — the daemon hands it
    // sizeof(BaseUniforms) so the extension lands after the base block.
    void testWriteHonoursOffset()
    {
        AnimationUniformExtension ext;
        ext.setIAnchorRectInTexture(QVector4D(0.25f, 0.5f, 0.75f, 1.0f));
        std::array<char, 688> buf{};
        ext.write(buf.data(), 16);
        float v = 0.0f;
        std::memcpy(&v, buf.data() + 16 + 32, sizeof(float)); // iAnchorRectInTexture.x
        QCOMPARE(v, 0.25f);
    }

    // A fresh extension reports dirty (the initial UBO upload must
    // happen). clearDirty() resets it; a setter with a CHANGED value
    // re-dirties; a setter with the SAME value does NOT — no redundant
    // upload under the static-popup reuse path.
    void testDirtyTracking()
    {
        AnimationUniformExtension ext;
        QVERIFY(ext.isDirty());

        ext.clearDirty();
        QVERIFY(!ext.isDirty());

        ext.setIAnchorRectInTexture(QVector4D(0.0f, 0.0f, 0.5f, 0.5f));
        QVERIFY(ext.isDirty());

        ext.clearDirty();
        ext.setIAnchorRectInTexture(QVector4D(0.0f, 0.0f, 0.5f, 0.5f));
        QVERIFY(!ext.isDirty());
    }
};

QTEST_APPLESS_MAIN(TestAnimationUniformExtension)
#include "test_animationuniformextension.moc"

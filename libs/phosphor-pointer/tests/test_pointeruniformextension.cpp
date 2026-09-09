// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderUniforms.h>
#include <PhosphorPointer/PointerUniformExtension.h>

#include <PhosphorShaders/BaseUniforms.h>

#include <QtTest/QtTest>

#include <array>
#include <cstring>

using namespace PhosphorPointerShaders;

/// The pointer UBO is a wire contract shared by the C++ mirror struct here and
/// the GLSL block in data/pointer/shared/pointer_uniforms.glsl. A field that
/// drifts by even one slot reads as garbage in the shader rather than failing
/// to compile, so the offsets are pinned in both directions and the extension's
/// write is checked against the same byte layout the shader will sample.
class TestPointerUniformExtension : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testTailLayoutMatchesTheGlslContract();
    void testWriteLandsAtTheDeclaredTailOffset();
    void testApplyPopulatesEveryDeclaredField();
    void testTrailIsTruncatedAndCountReported();
    void testUnusedTrailSlotsAreZeroed();
    void testDirtyFlagTracksRealChangesOnly();

private:
    /// Read the vec4 at @p tailByteOffset out of a buffer written by the
    /// extension, addressing it exactly as the shader's UBO would.
    static std::array<float, 4> vec4At(const std::vector<char>& buffer, size_t tailByteOffset)
    {
        std::array<float, 4> out{};
        std::memcpy(out.data(), buffer.data() + kPointerTailOffset + tailByteOffset, sizeof(out));
        return out;
    }
};

void TestPointerUniformExtension::testTailLayoutMatchesTheGlslContract()
{
    // These numbers are transcribed from the UBO comment block in
    // pointer_uniforms.glsl. Both sides must agree or the pack reads noise.
    QCOMPARE(sizeof(PhosphorShaders::BaseUniforms), size_t{672});
    QCOMPARE(kPointerTailOffset, size_t{672});
    QCOMPARE(sizeof(PointerUniformsTail), size_t{608});
    QCOMPARE(sizeof(PhosphorShaders::BaseUniforms) + sizeof(PointerUniformsTail), size_t{1280});

    QCOMPARE(offsetof(PointerUniformsTail, uPointerVelocity) + kPointerTailOffset, size_t{672});
    QCOMPARE(offsetof(PointerUniformsTail, uPointerPress) + kPointerTailOffset, size_t{688});
    QCOMPARE(offsetof(PointerUniformsTail, uPointerRelease) + kPointerTailOffset, size_t{704});
    QCOMPARE(offsetof(PointerUniformsTail, uPointerState) + kPointerTailOffset, size_t{720});
    QCOMPARE(offsetof(PointerUniformsTail, uCursorRect) + kPointerTailOffset, size_t{736});
    QCOMPARE(offsetof(PointerUniformsTail, uPointerFlags) + kPointerTailOffset, size_t{752});
    QCOMPARE(offsetof(PointerUniformsTail, uPointerTrail) + kPointerTailOffset, size_t{768});

    // The trail array is the tail's whole remainder, so a capacity change
    // would silently move nothing but would break the shader's loop bound.
    QCOMPARE(PointerShaderContract::kMaxTrailPoints, 32);
    QCOMPARE(sizeof(PointerUniformsTail::uPointerTrail), size_t{512});
}

void TestPointerUniformExtension::testWriteLandsAtTheDeclaredTailOffset()
{
    PointerUniformExtension ext;
    QCOMPARE(ext.extensionSize(), static_cast<int>(sizeof(PointerUniformsTail)));

    std::vector<char> buffer(1280, char{0x7f});
    ext.setVelocity(QVector2D(12.0f, -5.0f));
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));

    // Everything before the tail belongs to BaseUniforms and must be
    // untouched: the host fills that half through a different path.
    for (size_t i = 0; i < kPointerTailOffset; ++i) {
        QCOMPARE(buffer[i], char{0x7f});
    }

    const std::array<float, 4> velocity = vec4At(buffer, offsetof(PointerUniformsTail, uPointerVelocity));
    QCOMPARE(velocity[0], 12.0f);
    QCOMPARE(velocity[1], -5.0f);
    // .z carries the scalar speed so a pack can read magnitude without a sqrt.
    QCOMPARE(velocity[2], 13.0f);
}

void TestPointerUniformExtension::testApplyPopulatesEveryDeclaredField()
{
    // apply() is the path both hosts use, so it is what has to be complete.
    PointerFrameState state;
    state.velocity = QVector2D(3.0f, 4.0f);
    state.pressPos = QPointF(100.0, 200.0);
    state.pressSecondsSince = 0.25;
    state.pressButton = 2;
    state.releasePos = QPointF(110.0, 210.0);
    state.releaseSecondsSince = 0.10;
    state.releaseButton = 1;
    state.buttons = 5;
    state.idleSeconds = 0.5;
    state.scale = 2.0;
    state.cursorRect = QRectF(50.0, 60.0, 24.0, 24.0);
    state.hasSprite = true;
    state.trail.append(QVector4D(1.0f, 2.0f, 0.0f, 900.0f));
    state.trail.append(QVector4D(3.0f, 4.0f, 0.016f, 850.0f));

    PointerUniformExtension ext;
    ext.setReachLogicalPx(48.0);
    ext.apply(state);

    std::vector<char> buffer(1280, char{0});
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));

    const auto press = vec4At(buffer, offsetof(PointerUniformsTail, uPointerPress));
    QCOMPARE(press[0], 100.0f);
    QCOMPARE(press[1], 200.0f);
    QCOMPARE(press[2], 0.25f);
    QCOMPARE(press[3], 2.0f);

    const auto release = vec4At(buffer, offsetof(PointerUniformsTail, uPointerRelease));
    QCOMPARE(release[0], 110.0f);
    QCOMPARE(release[1], 210.0f);
    QCOMPARE(release[2], 0.10f);
    QCOMPARE(release[3], 1.0f);

    const auto pointerState = vec4At(buffer, offsetof(PointerUniformsTail, uPointerState));
    QCOMPARE(pointerState[0], 5.0f); // pressed-button mask
    QCOMPARE(pointerState[1], 0.5f); // seconds since motion
    QCOMPARE(pointerState[2], 2.0f); // logical-to-device scale
    QCOMPARE(pointerState[3], 2.0f); // filled trail points

    const auto cursorRect = vec4At(buffer, offsetof(PointerUniformsTail, uCursorRect));
    QCOMPARE(cursorRect[0], 50.0f);
    QCOMPARE(cursorRect[1], 60.0f);
    QCOMPARE(cursorRect[2], 24.0f);
    QCOMPARE(cursorRect[3], 24.0f);

    const auto flags = vec4At(buffer, offsetof(PointerUniformsTail, uPointerFlags));
    QCOMPARE(flags[0], 1.0f); // cursor sprite bound
    QCOMPARE(flags[1], 96.0f); // reach: 48 logical px at scale 2 — apply must not clobber it

    const auto newest = vec4At(buffer, offsetof(PointerUniformsTail, uPointerTrail));
    QCOMPARE(newest[0], 1.0f);
    QCOMPARE(newest[1], 2.0f);
    QCOMPARE(newest[2], 0.0f);
    QCOMPARE(newest[3], 900.0f);
}

void TestPointerUniformExtension::testTrailIsTruncatedAndCountReported()
{
    // A host feeding more points than the array holds must be clamped rather
    // than overrunning the tail into whatever follows the UBO.
    QList<QVector4D> overlong;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints + 10; ++i) {
        overlong.append(QVector4D(static_cast<float>(i), 0.0f, 0.0f, 0.0f));
    }

    PointerUniformExtension ext;
    ext.setTrail(overlong);

    std::vector<char> buffer(1280, char{0});
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));

    const auto pointerState = vec4At(buffer, offsetof(PointerUniformsTail, uPointerState));
    QCOMPARE(pointerState[3], static_cast<float>(PointerShaderContract::kMaxTrailPoints));

    const size_t lastSlot = offsetof(PointerUniformsTail, uPointerTrail)
        + static_cast<size_t>(PointerShaderContract::kMaxTrailPoints - 1) * 4 * sizeof(float);
    const auto last = vec4At(buffer, lastSlot);
    QCOMPARE(last[0], static_cast<float>(PointerShaderContract::kMaxTrailPoints - 1));
}

void TestPointerUniformExtension::testUnusedTrailSlotsAreZeroed()
{
    // Shaders loop to the constant capacity and break on the count, but a
    // stale point left in an unused slot still shows up in any pack that
    // reads the array directly, so the setter clears the remainder.
    PointerUniformExtension ext;

    QList<QVector4D> full;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints; ++i) {
        full.append(QVector4D(99.0f, 99.0f, 99.0f, 99.0f));
    }
    ext.setTrail(full);

    QList<QVector4D> shortened;
    shortened.append(QVector4D(1.0f, 1.0f, 0.0f, 0.0f));
    ext.setTrail(shortened);

    std::vector<char> buffer(1280, char{0});
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));

    const auto pointerState = vec4At(buffer, offsetof(PointerUniformsTail, uPointerState));
    QCOMPARE(pointerState[3], 1.0f);

    for (int i = 1; i < PointerShaderContract::kMaxTrailPoints; ++i) {
        const size_t slot = offsetof(PointerUniformsTail, uPointerTrail) + static_cast<size_t>(i) * 4 * sizeof(float);
        const auto entry = vec4At(buffer, slot);
        QCOMPARE(entry[0], 0.0f);
        QCOMPARE(entry[1], 0.0f);
        QCOMPARE(entry[2], 0.0f);
        QCOMPARE(entry[3], 0.0f);
    }
}

void TestPointerUniformExtension::testDirtyFlagTracksRealChangesOnly()
{
    // The render thread re-uploads on dirty. Setting the same value every
    // frame while the pointer is parked must not keep the upload alive.
    PointerUniformExtension ext;
    ext.clearDirty();
    QVERIFY(!ext.isDirty());

    ext.setVelocity(QVector2D(1.0f, 0.0f));
    QVERIFY(ext.isDirty());

    ext.clearDirty();
    ext.setVelocity(QVector2D(1.0f, 0.0f));
    QVERIFY(!ext.isDirty());

    ext.setVelocity(QVector2D(2.0f, 0.0f));
    QVERIFY(ext.isDirty());
}

QTEST_MAIN(TestPointerUniformExtension)
#include "test_pointeruniformextension.moc"

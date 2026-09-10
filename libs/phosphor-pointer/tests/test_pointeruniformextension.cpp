// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderUniforms.h>
#include <PhosphorPointer/PointerUniformExtension.h>

#include <PhosphorShaders/BaseUniforms.h>

#include <QtTest/QtTest>

#include <array>
#include <cstring>
#include <span>
#include <vector>

using namespace PhosphorPointerShaders;

namespace {

/// A frame state whose every lane is set to something other than its
/// default, so a setter that ignores its argument shows up as a stale lane.
PointerFrameState fullFrame()
{
    PointerFrameState state;
    state.velocity = QVector2D(3.0f, 4.0f);
    state.filteredSpeed = 4.5;
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
    state.trail[0] = QVector4D(1.0f, 2.0f, 0.0f, 900.0f);
    state.trail[1] = QVector4D(3.0f, 4.0f, 0.016f, 850.0f);
    state.trailCount = 2;
    return state;
}

} // namespace

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
    void testApplyingAnIdenticalFrameLeavesTheTailClean();

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
    ext.setVelocity(QVector2D(12.0f, -5.0f), 7.5);
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
    // .w carries the sampler's filtered speed, what pointerFilteredSpeed()
    // reads, so a gate costs one uniform read rather than a walk per fragment.
    QCOMPARE(velocity[3], 7.5f);
}

void TestPointerUniformExtension::testApplyPopulatesEveryDeclaredField()
{
    // apply() is the path both hosts use, so it is what has to be complete.
    const PointerFrameState state = fullFrame();

    PointerUniformExtension ext;
    ext.setReachLogicalPx(48.0);
    ext.apply(state);

    std::vector<char> buffer(1280, char{0});
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));

    const auto velocity = vec4At(buffer, offsetof(PointerUniformsTail, uPointerVelocity));
    QCOMPARE(velocity[0], 3.0f);
    QCOMPARE(velocity[1], 4.0f);
    QCOMPARE(velocity[2], 5.0f);
    QCOMPARE(velocity[3], 4.5f); // the sampler's filtered speed

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
    std::vector<QVector4D> overlong;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints + 10; ++i) {
        overlong.emplace_back(static_cast<float>(i), 0.0f, 0.0f, 0.0f);
    }

    PointerUniformExtension ext;
    ext.setTrail(std::span<const QVector4D>(overlong));

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

    std::vector<QVector4D> full;
    for (int i = 0; i < PointerShaderContract::kMaxTrailPoints; ++i) {
        full.emplace_back(99.0f, 99.0f, 99.0f, 99.0f);
    }
    ext.setTrail(std::span<const QVector4D>(full));

    const std::array<QVector4D, 1> shortened{QVector4D(1.0f, 1.0f, 0.0f, 0.0f)};
    ext.setTrail(std::span<const QVector4D>(shortened));

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

    ext.setVelocity(QVector2D(1.0f, 0.0f), 0.0);
    QVERIFY(ext.isDirty());

    ext.clearDirty();
    ext.setVelocity(QVector2D(1.0f, 0.0f), 0.0);
    QVERIFY(!ext.isDirty());

    ext.setVelocity(QVector2D(2.0f, 0.0f), 0.0);
    QVERIFY(ext.isDirty());
}

void TestPointerUniformExtension::testApplyingAnIdenticalFrameLeavesTheTailClean()
{
    // The same rule across the whole frame: apply() crosses every setter,
    // including the trail's per-slot compare and the reach lane it derives,
    // and none of them may raise dirty for a value already in the tail.
    PointerUniformExtension ext;
    ext.setReachLogicalPx(48.0);
    const PointerFrameState state = fullFrame();
    ext.apply(state);
    QVERIFY(ext.isDirty());

    ext.clearDirty();
    ext.apply(state);
    QVERIFY(!ext.isDirty());

    // And each lane still flips it when it really changes, so the clean
    // result above is a compare, not a setter that stopped writing.
    const auto expectDirtyAfter = [&](auto mutate) {
        ext.clearDirty();
        PointerFrameState changed = fullFrame();
        mutate(changed);
        ext.apply(changed);
        return ext.isDirty();
    };
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.velocity = QVector2D(9.0f, 4.0f);
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.pressSecondsSince = 0.5;
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.releaseButton = 3;
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.idleSeconds = 0.75;
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.cursorRect = QRectF(0.0, 0.0, 1.0, 1.0);
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.hasSprite = false;
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.trail[1].setX(30.0f);
    }));
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.trailCount = 1;
    }));
    // filteredSpeed shares uPointerVelocity with the velocity vector, so the
    // velocity case above would stay green if the .w write were dropped
    // entirely. This is the only case that moves that lane alone.
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.filteredSpeed = 9.0;
    }));
    // scale reaches TWO lanes by separate paths: uPointerState[2] directly,
    // and uPointerFlags[1] as the reach scaled by it. Nothing moved it on its
    // own, so a setFlagsLocked that stopped honouring the frame's scale left
    // every existing assertion green.
    QVERIFY(expectDirtyAfter([](PointerFrameState& s) {
        s.scale = 3.0;
    }));

    // The dirty bit says something changed, not that the RIGHT thing changed.
    // Both lanes are checked by value here, because the reach lane in
    // particular is a product the flags path recomputes rather than a field it
    // copies.
    PointerFrameState scaled = fullFrame();
    scaled.scale = 3.0;
    scaled.filteredSpeed = 9.0;
    ext.setReachLogicalPx(48.0);
    ext.apply(scaled);

    std::vector<char> buffer(1280, char{0});
    ext.write(buffer.data(), static_cast<int>(kPointerTailOffset));
    const auto stateLane = vec4At(buffer, offsetof(PointerUniformsTail, uPointerState));
    QCOMPARE(stateLane[2], 3.0f);
    const auto flags = vec4At(buffer, offsetof(PointerUniformsTail, uPointerFlags));
    // The reach lane is a PRODUCT the flags path recomputes from the frame's
    // scale, not a field it copies, which is exactly why the scale case above
    // needs a value check behind it.
    QCOMPARE(flags[1], 144.0f); // 48 logical px at scale 3
    const auto velocity = vec4At(buffer, offsetof(PointerUniformsTail, uPointerVelocity));
    QCOMPARE(velocity[3], 9.0f);
}

QTEST_MAIN(TestPointerUniformExtension)
#include "test_pointeruniformextension.moc"

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/BaseUniformProfile.h>
#include <PhosphorShaders/BaseUniforms.h>
#include <PhosphorShaders/IUboProfile.h>

#include <QtTest/QtTest>

#include <cstring>

using namespace PhosphorShaders;

namespace {

/// Build a fixed, fully-populated frame state. Pins didFullUploadOnce=true +
/// sceneDataDirty=false so the iDate wall-clock refresh path is skipped — that
/// makes fill() fully deterministic (iDate stays zero) for a byte-exact golden
/// comparison.
UboFrameState makeFixedState()
{
    UboFrameState s;
    s.time = 12.5f;
    s.timeHi = 1024.0f;
    s.timeDelta = 0.016f;
    s.frame = 700;
    s.bufferFeedback = false;
    s.bufferFeedbackCleared = false;
    s.width = 1920.0f;
    s.height = 1080.0f;
    s.mouseX = 480.0f;
    s.mouseY = 270.0f;
    s.isReversed = true;
    s.didFullUploadOnce = true; // deterministic: skip iDate wall-clock path
    s.sceneDataDirty = false;
    for (int i = 0; i < 8; ++i) {
        for (int c = 0; c < 4; ++c) {
            s.customParams[i][c] = static_cast<float>(i * 10 + c);
        }
    }
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 4; ++c) {
            s.customColors[i][c] = static_cast<float>(i) * 0.01f + static_cast<float>(c) * 0.001f;
        }
    }
    for (int i = 0; i < 4; ++i) {
        s.channelResolution[i][0] = static_cast<float>(256 + i);
        s.channelResolution[i][1] = static_cast<float>(128 + i);
        s.textureResolution[i][0] = static_cast<float>(64 + i);
        s.textureResolution[i][1] = static_cast<float>(32 + i);
    }
    s.audioSpectrumSize = 64;
    s.yUpInNDC = true; // exercise the Y-flip branch
    return s;
}

/// Independent transcription of the expected BaseUniforms bytes for the fixed
/// state above. Deliberately NOT sharing code with BaseUniformProfile::fill()
/// so the memcmp catches a divergence in either direction.
BaseUniforms makeReference(const UboFrameState& s)
{
    BaseUniforms u = {};

    // qt_Matrix: identity with Y-flip when yUpInNDC, qt_Opacity unset by fill()
    // after the memset (fill() does not touch qt_Opacity), so it stays at the
    // ctor seed of 1.0 — replicate that here.
    u.qt_Matrix[0] = 1.0f;
    u.qt_Matrix[5] = s.yUpInNDC ? -1.0f : 1.0f;
    u.qt_Matrix[10] = 1.0f;
    u.qt_Matrix[15] = 1.0f;
    u.qt_Opacity = 1.0f;

    u.iTime = s.time;
    u.iTimeDelta = s.timeDelta;
    u.iFrame = (s.bufferFeedback && !s.bufferFeedbackCleared) ? 0 : s.frame;
    u.iResolution[0] = s.width;
    u.iResolution[1] = s.height;
    u.iMouse[0] = s.mouseX;
    u.iMouse[1] = s.mouseY;
    u.iMouse[2] = s.width > 0 ? s.mouseX / s.width : 0.0f;
    u.iMouse[3] = s.height > 0 ? s.mouseY / s.height : 0.0f;
    // iDate left at 0 — deterministic state skips the wall-clock path.

    for (int i = 0; i < 8; ++i) {
        for (int c = 0; c < 4; ++c) {
            u.customParams[i][c] = s.customParams[i][c];
        }
    }
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 4; ++c) {
            u.customColors[i][c] = s.customColors[i][c];
        }
    }
    for (int i = 0; i < 4; ++i) {
        u.iChannelResolution[i][0] = s.channelResolution[i][0];
        u.iChannelResolution[i][1] = s.channelResolution[i][1];
        u.iChannelResolution[i][2] = 0.0f;
        u.iChannelResolution[i][3] = 0.0f;
    }
    u.iAudioSpectrumSize = s.audioSpectrumSize;
    u.iFlipBufferY = 1;
    u._pad_after_audioSpectrum[0] = 0;
    u._pad_after_audioSpectrum[1] = 0;
    for (int i = 0; i < 4; ++i) {
        u.iTextureResolution[i][0] = s.textureResolution[i][0];
        u.iTextureResolution[i][1] = s.textureResolution[i][1];
        u.iTextureResolution[i][2] = 0.0f;
        u.iTextureResolution[i][3] = 0.0f;
    }
    u.iTimeHi = s.timeHi;
    u.iIsReversed = s.isReversed ? 1 : 0;

    return u;
}

} // namespace

class TestBaseUniformProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void baseSize_is_672()
    {
        BaseUniformProfile profile;
        QCOMPARE(profile.baseSize(), static_cast<int>(sizeof(BaseUniforms)));
        QCOMPARE(profile.baseSize(), 672);
    }

    void golden_bytes_match_reference()
    {
        const UboFrameState state = makeFixedState();
        BaseUniformProfile profile;
        profile.fill(state);

        const BaseUniforms reference = makeReference(state);

        QCOMPARE(static_cast<int>(sizeof(reference)), profile.baseSize());
        const int rc = std::memcmp(profile.data(), &reference, sizeof(BaseUniforms));
        QCOMPARE(rc, 0);
    }

    void golden_bytes_match_reference_no_flip()
    {
        // Same golden comparison with yUpInNDC=false so the qt_Matrix[5] = +1
        // no-flip branch of fill() is pinned too — the true-only fixture above
        // would let a fill() bug that ignored yUpInNDC for qt_Matrix slip past.
        UboFrameState state = makeFixedState();
        state.yUpInNDC = false;
        BaseUniformProfile profile;
        profile.fill(state);

        const BaseUniforms reference = makeReference(state);

        QCOMPARE(static_cast<int>(sizeof(reference)), profile.baseSize());
        const int rc = std::memcmp(profile.data(), &reference, sizeof(BaseUniforms));
        QCOMPARE(rc, 0);
    }

    void app_fields_write_through()
    {
        BaseUniformProfile profile;
        QVERIFY(profile.hasAppFields());
        profile.setAppField0(42);
        profile.setAppField1(-7);
        const auto* u = static_cast<const BaseUniforms*>(profile.data());
        QCOMPARE(u->appField0, 42);
        QCOMPARE(u->appField1, -7);
    }

    void dirty_regions_match_legacy_dispatch()
    {
        using namespace UboRegions;
        BaseUniformProfile profile;

        // time only → K_TIME_BLOCK
        {
            auto r = profile.dirtyRegions(UboDirtyFlags{true, false, false, false});
            QCOMPARE(static_cast<int>(r.size()), 1);
            QCOMPARE(r[0].offset, static_cast<int>(K_TIME_BLOCK_OFFSET));
            QCOMPARE(r[0].size, static_cast<int>(K_TIME_BLOCK_SIZE));
        }
        // timeHi && !scene → K_TIME_HI
        {
            auto r = profile.dirtyRegions(UboDirtyFlags{false, true, false, false});
            QCOMPARE(static_cast<int>(r.size()), 1);
            QCOMPARE(r[0].offset, static_cast<int>(K_TIME_HI_OFFSET));
            QCOMPARE(r[0].size, static_cast<int>(K_TIME_HI_SIZE));
        }
        // scene → K_SCENE_HEADER (subsumes timeHi)
        {
            auto r = profile.dirtyRegions(UboDirtyFlags{false, true, true, true});
            QCOMPARE(static_cast<int>(r.size()), 1);
            QCOMPARE(r[0].offset, static_cast<int>(K_SCENE_HEADER_OFFSET));
            QCOMPARE(r[0].size, static_cast<int>(K_SCENE_HEADER_SIZE));
        }
        // appFields only → K_APP_FIELDS
        {
            auto r = profile.dirtyRegions(UboDirtyFlags{false, false, false, true});
            QCOMPARE(static_cast<int>(r.size()), 1);
            QCOMPARE(r[0].offset, static_cast<int>(K_APP_FIELDS_OFFSET));
            QCOMPARE(r[0].size, static_cast<int>(K_APP_FIELDS_SIZE));
        }
        // time + scene → two regions (K_TIME_BLOCK, K_SCENE_HEADER)
        {
            auto r = profile.dirtyRegions(UboDirtyFlags{true, false, true, false});
            QCOMPARE(static_cast<int>(r.size()), 2);
            QCOMPARE(r[0].offset, static_cast<int>(K_TIME_BLOCK_OFFSET));
            QCOMPARE(r[1].offset, static_cast<int>(K_SCENE_HEADER_OFFSET));
        }
    }

    void full_upload_regions_cover_struct()
    {
        BaseUniformProfile profile;
        auto r = profile.fullUploadRegions();
        QCOMPARE(static_cast<int>(r.size()), 1);
        QCOMPARE(r[0].offset, 0);
        QCOMPARE(r[0].size, profile.baseSize());
    }
};

QTEST_MAIN(TestBaseUniformProfile)
#include "test_baseuniformprofile.moc"

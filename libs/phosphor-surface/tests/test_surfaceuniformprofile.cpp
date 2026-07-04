// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceShaderUniforms.h>
#include <PhosphorSurface/SurfaceUniformProfile.h>

#include <PhosphorShaders/IUboProfile.h>

#include <QtTest/QtTest>

#include <cstddef>
#include <cstring>

using namespace PhosphorSurfaceShaders;

namespace {

PhosphorShaders::UboFrameState makeFixedState()
{
    PhosphorShaders::UboFrameState s;
    s.time = 3.25f;
    s.yUpInNDC = true;
    s.qtOpacity = 0.75f;
    s.surfaceScale = 2.0f;
    s.surfaceFocused = 1.0f;
    s.surfaceSize[0] = 800.0f;
    s.surfaceSize[1] = 600.0f;
    s.surfaceFrameTopLeft[0] = 4.0f;
    s.surfaceFrameTopLeft[1] = 8.0f;
    s.surfaceFrameSize[0] = 792.0f;
    s.surfaceFrameSize[1] = 584.0f;
    for (int i = 0; i < 8; ++i) {
        for (int c = 0; c < 4; ++c) {
            s.customParams[i][c] = static_cast<float>(i * 4 + c);
        }
    }
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 4; ++c) {
            s.customColors[i][c] = static_cast<float>(i) * 0.02f + static_cast<float>(c) * 0.002f;
        }
    }
    for (int i = 0; i < 4; ++i) {
        s.channelResolution[i][0] = static_cast<float>(100 + i);
        s.channelResolution[i][1] = static_cast<float>(50 + i);
    }
    return s;
}

SurfaceUniforms makeReference(const PhosphorShaders::UboFrameState& s)
{
    SurfaceUniforms u = {};
    u.qt_Matrix[0] = 1.0f;
    u.qt_Matrix[5] = s.yUpInNDC ? -1.0f : 1.0f;
    u.qt_Matrix[10] = 1.0f;
    u.qt_Matrix[15] = 1.0f;
    u.qt_Opacity = s.qtOpacity;
    u.uSurfaceScale = s.surfaceScale;
    u.uSurfaceFocused = s.surfaceFocused;
    u.iTime = s.time;
    u.uSurfaceSize[0] = s.surfaceSize[0];
    u.uSurfaceSize[1] = s.surfaceSize[1];
    u.uSurfaceFrameTopLeft[0] = s.surfaceFrameTopLeft[0];
    u.uSurfaceFrameTopLeft[1] = s.surfaceFrameTopLeft[1];
    u.uSurfaceFrameSize[0] = s.surfaceFrameSize[0];
    u.uSurfaceFrameSize[1] = s.surfaceFrameSize[1];
    // Daemon-pinned contract gates: no scene behind a daemon surface, and no
    // rule opacity (qt_Opacity carries host opacity) — mirrors fill().
    u.uHasBackdrop = 0.0f;
    u.uSurfaceOpacity = 1.0f;
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
    return u;
}

} // namespace

class TestSurfaceUniformProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void baseSize_is_560()
    {
        SurfaceUniformProfile profile;
        QCOMPARE(profile.baseSize(), static_cast<int>(sizeof(SurfaceUniforms)));
        QCOMPARE(profile.baseSize(), 560);
    }

    void golden_bytes_match_reference()
    {
        const PhosphorShaders::UboFrameState state = makeFixedState();
        SurfaceUniformProfile profile;
        profile.fill(state);

        const SurfaceUniforms reference = makeReference(state);
        QCOMPARE(static_cast<int>(sizeof(reference)), profile.baseSize());
        const int rc = std::memcmp(profile.data(), &reference, sizeof(SurfaceUniforms));
        QCOMPARE(rc, 0);
    }

    void no_app_fields()
    {
        SurfaceUniformProfile profile;
        QVERIFY(!profile.hasAppFields());
        // No-op default writes must not crash or corrupt the buffer.
        profile.setAppField0(99);
        profile.setAppField1(99);
    }

    void dirty_regions_matrix_plus_scene()
    {
        SurfaceUniformProfile profile;

        // No flags → no regions.
        QVERIFY(profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{}).empty());

        // Any flag → matrix {0,64} + scene {64, 560-64}.
        auto r = profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{false, false, true, false});
        QCOMPARE(static_cast<int>(r.size()), 2);
        QCOMPARE(r[0].offset, 0);
        QCOMPARE(r[0].size, 64);
        QCOMPARE(r[1].offset, static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)));
        QCOMPARE(r[1].size,
                 static_cast<int>(sizeof(SurfaceUniforms)) - static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)));
    }

    void full_upload_regions_cover_struct()
    {
        SurfaceUniformProfile profile;
        auto r = profile.fullUploadRegions();
        QCOMPARE(static_cast<int>(r.size()), 1);
        QCOMPARE(r[0].offset, 0);
        QCOMPARE(r[0].size, profile.baseSize());
    }
};

QTEST_MAIN(TestSurfaceUniformProfile)
#include "test_surfaceuniformprofile.moc"

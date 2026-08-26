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
    // Deliberately NOT 1.0: that is what a hardcoded field would produce,
    // so pinning it here would make the focus pass-through untestable.
    s.surfaceFocused = 0.35f;
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
    s.audioSpectrumSize = 12;
    s.mouseX = 320.0f;
    s.mouseY = 240.0f;
    for (int i = 0; i < 4; ++i) {
        s.textureResolution[i][0] = static_cast<float>(64 + i);
        s.textureResolution[i][1] = static_cast<float>(32 + i);
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
    // uHasBackdrop tracks the state's gate: a daemon surface has no live scene
    // behind it, but a host can bind a stand-in (the desktop wallpaper) and the
    // node raises this when that binding is live. Still pinned is the rule
    // opacity — qt_Opacity carries host opacity. Mirrors fill().
    u.uHasBackdrop = s.hasBackdrop;
    for (std::size_t i = 0; i < std::size(u.uBackdropRect); ++i) {
        u.uBackdropRect[i] = s.backdropRect[i];
    }
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
    u.iAudioSpectrumSize = s.audioSpectrumSize;
    // Cursor (device px) with .zw normalized by the surface size — mirrors
    // fill()'s convention (negative passthrough carries the off-surface
    // sentinel; the fixed state uses an in-surface position).
    u.iMouse[0] = s.mouseX;
    u.iMouse[1] = s.mouseY;
    u.iMouse[2] = s.surfaceSize[0] > 0.0f ? s.mouseX / s.surfaceSize[0] : s.mouseX;
    u.iMouse[3] = s.surfaceSize[1] > 0.0f ? s.mouseY / s.surfaceSize[1] : s.mouseY;
    for (int i = 0; i < 4; ++i) {
        u.iTextureResolution[i][0] = s.textureResolution[i][0];
        u.iTextureResolution[i][1] = s.textureResolution[i][1];
        u.iTextureResolution[i][2] = 0.0f;
        u.iTextureResolution[i][3] = 0.0f;
    }
    return u;
}

} // namespace

class TestSurfaceUniformProfile : public QObject
{
    Q_OBJECT

private:
    /// One float read out of the filled block at a byte offset, so a test can
    /// assert a single member landed where the std140 layout says it does
    /// rather than only comparing whole blocks.
    static float readFloatAt(const SurfaceUniformProfile& profile, std::size_t offset)
    {
        float out = 0.0f;
        std::memcpy(&out, static_cast<const char*>(profile.data()) + offset, sizeof(out));
        return out;
    }

private Q_SLOTS:
    void baseSize_is_672()
    {
        SurfaceUniformProfile profile;
        QCOMPARE(profile.baseSize(), static_cast<int>(sizeof(SurfaceUniforms)));
        QCOMPARE(profile.baseSize(), 672);
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

    void golden_bytes_match_reference_no_flip()
    {
        // Cover the qt_Matrix[5] = +1 no-flip branch of fill(); makeFixedState
        // sets yUpInNDC=true, so without this the +1 path stays untested.
        PhosphorShaders::UboFrameState state = makeFixedState();
        state.yUpInNDC = false;
        SurfaceUniformProfile profile;
        profile.fill(state);

        const SurfaceUniforms reference = makeReference(state);
        QCOMPARE(static_cast<int>(sizeof(reference)), profile.baseSize());
        const int rc = std::memcmp(profile.data(), &reference, sizeof(SurfaceUniforms));
        QCOMPARE(rc, 0);
    }

    /// The backdrop gate reaches the UBO. A needsBackdrop pack (the glass /
    /// blur family) branches on uHasBackdrop to choose between sampling the
    /// backdrop and its fallback appearance, so a gate stuck at 0 is exactly
    /// the "blur renders as a flat tint" failure — silent, and indistinguishable
    /// from the pack simply having no backdrop to show.
    void backdrop_gate_reaches_the_ubo()
    {
        PhosphorShaders::UboFrameState state = makeFixedState();
        SurfaceUniformProfile profile;

        // Default: nothing bound, pack takes its fallback.
        state.hasBackdrop = 0.0f;
        profile.fill(state);
        QCOMPARE(readFloatAt(profile, offsetof(SurfaceUniforms, uHasBackdrop)), 0.0f);

        // Raised once the node sees a live backdrop binding.
        state.hasBackdrop = 1.0f;
        profile.fill(state);
        QCOMPARE(readFloatAt(profile, offsetof(SurfaceUniforms, uHasBackdrop)), 1.0f);

        // And the whole block still matches the reference with it raised, so
        // the gate cannot be landing on top of a neighbouring member.
        const SurfaceUniforms reference = makeReference(state);
        QCOMPARE(std::memcmp(profile.data(), &reference, sizeof(SurfaceUniforms)), 0);
    }

    /// std140 offsets, as LITERALS.
    ///
    /// The golden-byte tests above cannot catch a layout change: makeReference
    /// builds the struct BY MEMBER NAME, so reordering two same-typed members
    /// moves the reference and the profile together and the memcmp still
    /// passes — while every shader, which addresses by offset, breaks. These
    /// numbers are the contract data/surface/shared/surface_uniforms.glsl
    /// declares, so they must be spelled out rather than derived from the
    /// struct they are meant to police.
    void std140_offsets_are_pinned()
    {
        QCOMPARE(static_cast<int>(sizeof(SurfaceUniforms)), 672);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)), 64);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceScale)), 68);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceFocused)), 72);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, iTime)), 76);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceSize)), 80);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceFrameTopLeft)), 88);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceFrameSize)), 96);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uHasBackdrop)), 104);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uSurfaceOpacity)), 108);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, customParams)), 112);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, customColors)), 240);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, iChannelResolution)), 496);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, iAudioSpectrumSize)), 560);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, iMouse)), 576);
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, iTextureResolution)), 592);
        // The tail member, and so the one a future append or reorder is most
        // likely to move. Pinned with a literal here rather than left to the
        // offsetof-derived assertions elsewhere in this file, which cannot
        // police the struct because they read their expected value from it.
        QCOMPARE(static_cast<int>(offsetof(SurfaceUniforms, uBackdropRect)), 656);
    }

    /// iMouse.zw on a surface whose size has not been pushed yet.
    ///
    /// The normalized pair divides by uSurfaceSize, so the zero-size case has
    /// its own branch — and that branch runs on the first frames of a freshly
    /// created decoration item, before geometry lands. A pack testing
    /// `iMouse.z < 0.0` for "no hover" must not read a phantom hover at the
    /// top-left there.
    void mouse_normalization_survives_a_zero_surface_size()
    {
        PhosphorShaders::UboFrameState state = makeFixedState();
        state.surfaceSize[0] = 0.0f;
        state.surfaceSize[1] = 0.0f;
        state.mouseX = -1.0f;
        state.mouseY = -1.0f;

        SurfaceUniformProfile profile;
        profile.fill(state);
        const int base = static_cast<int>(offsetof(SurfaceUniforms, iMouse));
        // The raw pair keeps the off-surface sentinel.
        QCOMPARE(readFloatAt(profile, base), -1.0f);
        QCOMPARE(readFloatAt(profile, base + 4), -1.0f);
        // The normalized pair must agree with it rather than reading as a
        // hover at the origin. Compared against the exact pass-through value
        // rather than just `< 0.0f`: dividing by a zero surfaceSize yields
        // -inf, which is also strictly negative, so a `< 0.0f` assertion
        // passes whether or not the guard this slot exists to pin is there.
        QCOMPARE(readFloatAt(profile, base + 8), -1.0f);
        QCOMPARE(readFloatAt(profile, base + 12), -1.0f);
    }

    /// The backdrop placement rect reaches the UBO.
    ///
    /// A daemon or preview host binds ONE desktop-sized wallpaper to every
    /// surface it decorates, so this is what tells each which part of it lies
    /// behind that surface. Stuck at the default every surface would sample the
    /// whole desktop squeezed into its own box — visible, but easy to mistake
    /// for the pack simply looking wrong.
    void backdrop_rect_reaches_the_ubo()
    {
        PhosphorShaders::UboFrameState state = makeFixedState();
        SurfaceUniformProfile profile;

        // The default is the whole texture, which is what a compositor host
        // (whose capture already covers the window) and a host with no
        // placement to offer both want.
        profile.fill(state);
        const int base = static_cast<int>(offsetof(SurfaceUniforms, uBackdropRect));
        QCOMPARE(readFloatAt(profile, base), 0.0f);
        QCOMPARE(readFloatAt(profile, base + 4), 0.0f);
        QCOMPARE(readFloatAt(profile, base + 8), 1.0f);
        QCOMPARE(readFloatAt(profile, base + 12), 1.0f);

        // A narrowed slice: a surface sitting in the lower-right quadrant.
        state.backdropRect[0] = 0.5f;
        state.backdropRect[1] = 0.25f;
        state.backdropRect[2] = 0.5f;
        state.backdropRect[3] = 0.75f;
        profile.fill(state);
        QCOMPARE(readFloatAt(profile, base), 0.5f);
        QCOMPARE(readFloatAt(profile, base + 4), 0.25f);
        QCOMPARE(readFloatAt(profile, base + 8), 0.5f);
        QCOMPARE(readFloatAt(profile, base + 12), 0.75f);

        // And the whole block still matches the reference with it set, so the
        // appended member cannot be landing on a neighbour's bytes.
        const SurfaceUniforms reference = makeReference(state);
        QCOMPARE(std::memcmp(profile.data(), &reference, sizeof(SurfaceUniforms)), 0);
    }

    void no_app_fields()
    {
        SurfaceUniformProfile profile;
        QVERIFY(!profile.hasAppFields());
        // No-op default writes must not crash OR silently mutate the buffer.
        // Snapshot the bytes, run the no-op setters, and assert nothing moved.
        const int sz = profile.baseSize();
        const QByteArray before(static_cast<const char*>(profile.data()), sz);
        profile.setAppField0(99);
        profile.setAppField1(99);
        const int rc = std::memcmp(profile.data(), before.constData(), static_cast<size_t>(sz));
        QCOMPARE(rc, 0);
    }

    void dirty_regions_matrix_plus_scene()
    {
        SurfaceUniformProfile profile;

        // No flags → no regions.
        QVERIFY(profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{}).empty());

        // Any relevant flag → matrix {0,64} + scene {64, 672-64}.
        //
        // Designated rather than positional: UboDirtyFlags is four bools, so
        // inserting or reordering one silently retargets a positional
        // initializer at a different flag with no compile error.
        auto r = profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{.sceneData = true});
        QCOMPARE(static_cast<int>(r.size()), 2);
        QCOMPARE(r[0].offset, 0);
        QCOMPARE(r[0].size, 64);
        QCOMPARE(r[1].offset, static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)));
        QCOMPARE(r[1].size,
                 static_cast<int>(sizeof(SurfaceUniforms)) - static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)));

        // Each contributing flag on its own, so dropping any one of them from
        // the predicate is caught. Asserted individually because the combined
        // case above passes on a predicate that consults only one of the three.
        QCOMPARE(static_cast<int>(profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{.time = true}).size()), 2);
        QCOMPARE(static_cast<int>(profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{.timeHi = true}).size()), 2);

        // appFields is deliberately NOT consulted: this profile has no
        // app-fields region, so an appFields-only signal carries nothing it
        // uploads. Pinned, because adding it to the predicate would make every
        // such signal re-upload the whole struct and nothing else would notice.
        QVERIFY(profile.dirtyRegions(PhosphorShaders::UboDirtyFlags{.appFields = true}).empty());
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

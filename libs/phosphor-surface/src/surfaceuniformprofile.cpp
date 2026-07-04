// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurface/SurfaceUniformProfile.h>

#include <cstddef>
#include <cstring>
#include <iterator>

namespace PhosphorSurfaceShaders {

namespace {

// std140 byte offsets/sizes for the surface UBO upload regions, pinned by the
// SurfaceUniforms static_asserts. The leading mat4 is its own region (it
// carries the per-frame Y-flip / transform); everything from qt_Opacity to the
// end of the struct is the "scene" region.
constexpr int kMatrixOffset = 0;
constexpr int kMatrixSize = 64; // mat4
constexpr int kSceneOffset = static_cast<int>(offsetof(SurfaceUniforms, qt_Opacity)); // 64
constexpr int kSceneSize = static_cast<int>(sizeof(SurfaceUniforms)) - kSceneOffset; // qt_Opacity..end

} // namespace

SurfaceUniformProfile::SurfaceUniformProfile()
{
    // Seed identity qt_Matrix + qt_Opacity=1.0, mirroring BaseUniformProfile's
    // lead-in so the shared engine sees a sane transform before the first fill.
    m_u.qt_Matrix[0] = 1.0f;
    m_u.qt_Matrix[5] = 1.0f;
    m_u.qt_Matrix[10] = 1.0f;
    m_u.qt_Matrix[15] = 1.0f;
    m_u.qt_Opacity = 1.0f;
}

void SurfaceUniformProfile::fill(const PhosphorShaders::UboFrameState& state)
{
    // qt_Matrix: identity with the NDC Y-flip baked into the m11 slot (column-
    // major index 5), same convention as the overlay profile.
    std::memset(m_u.qt_Matrix, 0, sizeof(m_u.qt_Matrix));
    m_u.qt_Matrix[0] = 1.0f;
    m_u.qt_Matrix[5] = state.yUpInNDC ? -1.0f : 1.0f;
    m_u.qt_Matrix[10] = 1.0f;
    m_u.qt_Matrix[15] = 1.0f;

    m_u.qt_Opacity = state.qtOpacity;
    m_u.uSurfaceScale = state.surfaceScale;
    m_u.uSurfaceFocused = state.surfaceFocused;
    m_u.iTime = state.time;

    m_u.uSurfaceSize[0] = state.surfaceSize[0];
    m_u.uSurfaceSize[1] = state.surfaceSize[1];
    m_u.uSurfaceFrameTopLeft[0] = state.surfaceFrameTopLeft[0];
    m_u.uSurfaceFrameTopLeft[1] = state.surfaceFrameTopLeft[1];
    m_u.uSurfaceFrameSize[0] = state.surfaceFrameSize[0];
    m_u.uSurfaceFrameSize[1] = state.surfaceFrameSize[1];

    // The daemon NEVER has a scene behind a surface, so uHasBackdrop is
    // pinned 0 here; only the compositor branch (classic uniforms, not this
    // UBO) can raise its counterpart.
    m_u.uHasBackdrop = 0.0f;
    // No rule opacity on the daemon — qt_Opacity carries host opacity.
    m_u.uSurfaceOpacity = 1.0f;

    // Loop bounds derive from the destination array extents (pinned by the
    // SurfaceUniforms static_asserts) so they can't silently under-copy if the
    // std140 layout ever grows.
    for (std::size_t i = 0; i < std::size(m_u.customParams); ++i) {
        m_u.customParams[i][0] = state.customParams[i][0];
        m_u.customParams[i][1] = state.customParams[i][1];
        m_u.customParams[i][2] = state.customParams[i][2];
        m_u.customParams[i][3] = state.customParams[i][3];
    }
    for (std::size_t i = 0; i < std::size(m_u.customColors); ++i) {
        m_u.customColors[i][0] = state.customColors[i][0];
        m_u.customColors[i][1] = state.customColors[i][1];
        m_u.customColors[i][2] = state.customColors[i][2];
        m_u.customColors[i][3] = state.customColors[i][3];
    }
    for (std::size_t i = 0; i < std::size(m_u.iChannelResolution); ++i) {
        m_u.iChannelResolution[i][0] = state.channelResolution[i][0];
        m_u.iChannelResolution[i][1] = state.channelResolution[i][1];
        m_u.iChannelResolution[i][2] = 0.0f;
        m_u.iChannelResolution[i][3] = 0.0f;
    }
}

PhosphorShaders::UboUploadRegionList
SurfaceUniformProfile::dirtyRegions(const PhosphorShaders::UboDirtyFlags& flags) const
{
    PhosphorShaders::UboUploadRegionList regions;

    // The surface UBO has no app-fields region (hasAppFields() is false), so
    // flags.appFields is deliberately NOT consulted — an appFields-only dirty
    // signal carries no state this profile uploads. The matrix (transform /
    // Y-flip) is its own granular region; everything else (opacity, geometry,
    // time, params, colours, channel sizes) lives in the scene region. Any
    // relevant dirty signal refreshes both — the surface UBO is small enough
    // that the overlay's finer-grained time/timeHi split buys nothing here.
    const bool anyDirty = flags.time || flags.timeHi || flags.sceneData;
    if (anyDirty) {
        regions.push(PhosphorShaders::UboUploadRegion{kMatrixOffset, kMatrixSize});
        regions.push(PhosphorShaders::UboUploadRegion{kSceneOffset, kSceneSize});
    }

    return regions;
}

} // namespace PhosphorSurfaceShaders

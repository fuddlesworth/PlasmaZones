// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/BaseUniformProfile.h>

#include <QDateTime>

#include <cstring>

namespace PhosphorShaders {

BaseUniformProfile::BaseUniformProfile()
{
    // Seed identity qt_Matrix + qt_Opacity=1.0 (moved from the legacy
    // ShaderNodeRhi ctor). m_u is value-initialized to zero by the in-class
    // initializer, so only the diagonal + opacity need setting here.
    m_u.qt_Matrix[0] = 1.0f;
    m_u.qt_Matrix[5] = 1.0f;
    m_u.qt_Matrix[10] = 1.0f;
    m_u.qt_Matrix[15] = 1.0f;
    m_u.qt_Opacity = 1.0f;
}

// ============================================================================
// fill — verbatim transcription of the legacy syncBaseUniforms() body, with
// the iFlipBufferY=1 and qt_Matrix Y-flip folded in from uploadDirtyTextures()
// (both have no QRhi dependency once yUpInNDC is carried in the frame state).
// ============================================================================

void BaseUniformProfile::fill(const UboFrameState& state)
{
    // NDC Y-orientation correction baked into qt_Matrix, folded in from the
    // legacy uploadDirtyTextures() which set this EVERY frame. Identity with the
    // Y-scale (column-major index 5 / m11) negated only on Y-up-in-NDC backends
    // (OpenGL); plain identity on Vulkan. Direct-to-window animation vertex
    // stages apply it as `gl_Position = qt_Matrix * vec4(position, 0, 1)`;
    // overlay/zone stages (zone.vert) ignore it. Dropping this (the ctor seeds a
    // FLIPLESS identity) left daemon animation shaders rendering Y-inverted on
    // OpenGL — fill() must reassert it per the carried yUpInNDC, exactly as the
    // sibling SurfaceUniformProfile does.
    std::memset(m_u.qt_Matrix, 0, sizeof(m_u.qt_Matrix));
    m_u.qt_Matrix[0] = 1.0f;
    m_u.qt_Matrix[5] = state.yUpInNDC ? -1.0f : 1.0f;
    m_u.qt_Matrix[10] = 1.0f;
    m_u.qt_Matrix[15] = 1.0f;

    // Split full-precision m_time (double) into iTime (wrapped lo) + iTimeHi (wrap offset)
    m_u.iTime = state.time;
    m_u.iTimeHi = state.timeHi;
    m_u.iTimeDelta = state.timeDelta;
    // When feedback buffers haven't been cleared yet, override iFrame to 0
    m_u.iFrame = (state.bufferFeedback && !state.bufferFeedbackCleared) ? 0 : state.frame;
    m_u.iResolution[0] = state.width;
    m_u.iResolution[1] = state.height;
    m_u.iMouse[0] = state.mouseX;
    m_u.iMouse[1] = state.mouseY;
    m_u.iMouse[2] = state.width > 0 ? state.mouseX / state.width : 0.0f;
    m_u.iMouse[3] = state.height > 0 ? state.mouseY / state.height : 0.0f;
    m_u.iIsReversed = state.isReversed ? 1 : 0;
    // iDate only advances once per second. m_sceneDataDirty is set by every
    // mouse-move/resize event, so naïvely recomputing iDate whenever it's
    // true would hit QDateTime::currentDateTime() at 60+ Hz during
    // interaction. Guard with a 1-second cached timestamp — iDate still
    // refreshes during idle (sceneDataDirty remains set for the first frame
    // of each redraw cycle), but we skip ~60 redundant calls per second.
    if (!state.didFullUploadOnce
        || (state.sceneDataDirty
            && (m_lastDateRefreshMs == 0 || (QDateTime::currentMSecsSinceEpoch() - m_lastDateRefreshMs) >= 1000))) {
        const QDateTime now = QDateTime::currentDateTime();
        m_lastDateRefreshMs = now.toMSecsSinceEpoch();
        m_u.iDate[0] = static_cast<float>(now.date().year());
        m_u.iDate[1] = static_cast<float>(now.date().month());
        m_u.iDate[2] = static_cast<float>(now.date().day());
        m_u.iDate[3] = static_cast<float>(now.time().hour() * 3600 + now.time().minute() * 60 + now.time().second()
                                          + now.time().msec() / 1000.0);
    }

    // appField0/appField1: left as-is (set by setAppField0/1 or extension)

    // Custom params
    for (int i = 0; i < 8; ++i) {
        m_u.customParams[i][0] = state.customParams[i][0];
        m_u.customParams[i][1] = state.customParams[i][1];
        m_u.customParams[i][2] = state.customParams[i][2];
        m_u.customParams[i][3] = state.customParams[i][3];
    }

    // Custom colors
    for (int i = 0; i < 16; ++i) {
        m_u.customColors[i][0] = state.customColors[i][0];
        m_u.customColors[i][1] = state.customColors[i][1];
        m_u.customColors[i][2] = state.customColors[i][2];
        m_u.customColors[i][3] = state.customColors[i][3];
    }

    // iChannelResolution — the node resolves live channel sizes (multipass /
    // single-buffer / unset → 1.0) into state.channelResolution[i].xy.
    for (int i = 0; i < 4; ++i) {
        m_u.iChannelResolution[i][0] = state.channelResolution[i][0];
        m_u.iChannelResolution[i][1] = state.channelResolution[i][1];
        m_u.iChannelResolution[i][2] = 0.0f;
        m_u.iChannelResolution[i][3] = 0.0f;
    }
    m_u.iAudioSpectrumSize = state.audioSpectrumSize;

    // iFlipBufferY: always 1 (folded in from uploadDirtyTextures()).
    m_u.iFlipBufferY = 1;
    m_u._pad_after_audioSpectrum[0] = 0;
    m_u._pad_after_audioSpectrum[1] = 0;

    // User texture resolutions (bindings 7-10) — node resolves live.
    for (int i = 0; i < 4; ++i) {
        m_u.iTextureResolution[i][0] = state.textureResolution[i][0];
        m_u.iTextureResolution[i][1] = state.textureResolution[i][1];
        m_u.iTextureResolution[i][2] = 0.0f;
        m_u.iTextureResolution[i][3] = 0.0f;
    }
}

// ============================================================================
// dirtyRegions — exact legacy UboRegions dispatch.
//
//   time      → K_TIME_BLOCK
//   timeHi && !sceneData → K_TIME_HI (granular wrap-only path)
//   sceneData → K_SCENE_HEADER (subsumes appFields, timeHi, iIsReversed)
//   else if appFields → K_APP_FIELDS
//
// Broader-subsumes-narrower: when sceneData fires it covers the appFields and
// timeHi regions, matching the upload site's else-if structure. The full /
// !didFullUploadOnce path is handled by the caller via fullUploadRegions().
// ============================================================================

std::vector<UboUploadRegion> BaseUniformProfile::dirtyRegions(const UboDirtyFlags& flags) const
{
    using namespace UboRegions;

    std::vector<UboUploadRegion> regions;

    if (flags.time) {
        regions.push_back(UboUploadRegion{static_cast<int>(K_TIME_BLOCK_OFFSET), static_cast<int>(K_TIME_BLOCK_SIZE)});
    }
    if (flags.timeHi && !flags.sceneData) {
        regions.push_back(UboUploadRegion{static_cast<int>(K_TIME_HI_OFFSET), static_cast<int>(K_TIME_HI_SIZE)});
    }
    if (flags.sceneData) {
        regions.push_back(
            UboUploadRegion{static_cast<int>(K_SCENE_HEADER_OFFSET), static_cast<int>(K_SCENE_HEADER_SIZE)});
    } else if (flags.appFields) {
        regions.push_back(UboUploadRegion{static_cast<int>(K_APP_FIELDS_OFFSET), static_cast<int>(K_APP_FIELDS_SIZE)});
    }

    return regions;
}

} // namespace PhosphorShaders

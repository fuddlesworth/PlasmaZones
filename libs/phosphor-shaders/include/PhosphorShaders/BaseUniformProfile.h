// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/BaseUniforms.h>
#include <PhosphorShaders/IUboProfile.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <QtGlobal>

namespace PhosphorShaders {

/// IUboProfile implementation for the overlay/animation runtime — the legacy
/// 672-byte BaseUniforms layout.
///
/// fill() is a faithful transcription of the historical
/// ShaderNodeRhi::syncBaseUniforms() body (plus the iFlipBufferY=1 and the
/// qt_Matrix Y-flip that previously lived in uploadDirtyTextures(), folded in
/// here because they have no QRhi dependency once yUpInNDC is carried in the
/// frame state). The one intentional departure: the iMouse normalized
/// components divide in float (the frame state carries floats) rather than the
/// historical double, a last-ULP difference the golden test pins to the float
/// path. dirtyRegions() reproduces the exact legacy UboRegions dispatch.
class PHOSPHORSHADERS_EXPORT BaseUniformProfile : public IUboProfile
{
public:
    BaseUniformProfile();
    ~BaseUniformProfile() override = default;

    int baseSize() const override
    {
        return static_cast<int>(sizeof(BaseUniforms));
    }

    const void* data() const override
    {
        return &m_u;
    }
    void* mutableData() override
    {
        return &m_u;
    }

    void fill(const UboFrameState& state) override;

    UboUploadRegionList dirtyRegions(const UboDirtyFlags& flags) const override;

    bool hasAppFields() const override
    {
        return true;
    }
    void setAppField0(int value) override
    {
        m_u.appField0 = value;
    }
    void setAppField1(int value) override
    {
        m_u.appField1 = value;
    }

private:
    BaseUniforms m_u = {};

    /// Epoch-ms of the last iDate recomputation. Throttles
    /// QDateTime::currentDateTime() to once per second during mouse-driven
    /// scene-header churn (iDate only advances at 1 Hz anyway). Owned by the
    /// profile now that the iDate fill lives here.
    qint64 m_lastDateRefreshMs = 0;
};

} // namespace PhosphorShaders

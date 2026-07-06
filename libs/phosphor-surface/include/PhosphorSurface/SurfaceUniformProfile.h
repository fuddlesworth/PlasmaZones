// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/SurfaceShaderUniforms.h>
#include <PhosphorSurface/phosphorsurface_export.h>

#include <PhosphorShaders/IUboProfile.h>

namespace PhosphorSurfaceShaders {

/// IUboProfile implementation for the surface-decoration runtime — the leaner
/// 576-byte SurfaceUniforms layout.
///
/// Reuses the shared PhosphorRendering::ShaderNodeRhi engine with the surface
/// UBO. The surface binding map (UBO@0, iChannel0-3@2-5, uTexture0@7) is a
/// strict subset of the overlay map, so only the UBO concern differs — this
/// class is that difference.
///
/// fill() reads the surface-only fields of UboFrameState (qtOpacity,
/// surfaceScale, surfaceFocused, time, surfaceSize/FrameTopLeft/FrameSize) plus
/// the shared customParams/customColors/channelResolution; it ignores the
/// overlay-only fields BaseUniformProfile uses.
class PHOSPHORSURFACE_EXPORT SurfaceUniformProfile : public PhosphorShaders::IUboProfile
{
public:
    SurfaceUniformProfile();
    ~SurfaceUniformProfile() override = default;

    int baseSize() const override
    {
        return static_cast<int>(sizeof(SurfaceUniforms));
    }

    const void* data() const override
    {
        return &m_u;
    }
    void* mutableData() override
    {
        return &m_u;
    }

    void fill(const PhosphorShaders::UboFrameState& state) override;

    PhosphorShaders::UboUploadRegionList dirtyRegions(const PhosphorShaders::UboDirtyFlags& flags) const override;

    // Surface UBO has no consumer escape-hatch int slots.
    bool hasAppFields() const override
    {
        return false;
    }

private:
    SurfaceUniforms m_u = {};
};

} // namespace PhosphorSurfaceShaders

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QtGlobal>

#include <array>
#include <cstddef>

namespace PhosphorShaders {

/// A contiguous byte range inside the UBO to (re)upload via
/// QRhiResourceUpdateBatch::updateDynamicBuffer. Offsets/sizes are in bytes
/// relative to the start of the UBO's base region (offset 0).
struct UboUploadRegion
{
    int offset = 0;
    int size = 0;
};

/// Fixed-capacity list of upload regions. dirtyRegions() runs on the render
/// hot path every dirty (animating) frame, so the dispatch must stay
/// allocation-free — a profile never emits more than a few disjoint regions,
/// and kCapacity bounds that by contract.
class UboUploadRegionList
{
public:
    static constexpr int kCapacity = 4;

    void push(const UboUploadRegion& region)
    {
        // Debug-assert AND release-guard the same bound: a profile emitting
        // more than kCapacity regions is a programming error, and silently
        // dropping the overflow (worst case: one region skipped this frame)
        // beats writing out of bounds.
        Q_ASSERT(m_count < kCapacity);
        if (m_count < kCapacity) {
            m_regions[static_cast<size_t>(m_count++)] = region;
        }
    }
    int size() const
    {
        return m_count;
    }
    bool empty() const
    {
        return m_count == 0;
    }
    const UboUploadRegion& operator[](int i) const
    {
        // Match push()'s defensive style: assert the caller stays within the
        // populated range. Callers iterate [0, size()) via begin()/end(), so a
        // stray index is a programming error.
        Q_ASSERT(i >= 0 && i < m_count);
        return m_regions[static_cast<size_t>(i)];
    }
    const UboUploadRegion* begin() const
    {
        return m_regions.data();
    }
    const UboUploadRegion* end() const
    {
        return m_regions.data() + m_count;
    }

private:
    std::array<UboUploadRegion, kCapacity> m_regions{};
    int m_count = 0;
};

/// Which conceptual blocks of the UBO changed since the last upload. The
/// render node maps its own granular dirty flags onto these, and the profile
/// translates them into concrete UboUploadRegion ranges via dirtyRegions().
///
/// The four flags mirror the legacy ShaderNodeRhi dirty model exactly:
///   time      → the small (iTime/iTimeDelta/iFrame) animation block
///   timeHi    → the wrap-offset-only granular path (iTimeHi)
///   sceneData → the broad scene-header block (resolution..end)
///   appFields → the tiny consumer escape-hatch block (appField0/1)
struct UboDirtyFlags
{
    bool time = false;
    bool timeHi = false;
    bool sceneData = false;
    bool appFields = false;
};

/// Snapshot of every node-side value a UBO profile's fill() may read for a
/// single frame. The render node populates this from its members during the
/// sync phase and hands it to fill() — keeping all node→UBO data flow on one
/// explicit seam. A given profile reads only the subset it needs:
/// BaseUniformProfile ignores the surface-only fields; a SurfaceUniformProfile
/// ignores the overlay-only ones.
struct UboFrameState
{
    // ── Timing (overlay/animation) ─────────────────────────────────────
    /// Wrapped low part of elapsed seconds (m_time - m_timeHi).
    float time = 0.0f;
    /// Wrap-offset high part.
    float timeHi = 0.0f;
    float timeDelta = 0.0f;
    int frame = 0;

    // ── Feedback-buffer state (drives iFrame override) ─────────────────
    bool bufferFeedback = false;
    bool bufferFeedbackCleared = false;

    // ── Resolution ─────────────────────────────────────────────────────
    float width = 0.0f;
    float height = 0.0f;

    // ── Mouse ──────────────────────────────────────────────────────────
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // ── Direction signal ───────────────────────────────────────────────
    bool isReversed = false;

    // ── iDate throttle gating ──────────────────────────────────────────
    /// Whether the first full upload has happened. The profile uses this
    /// (with sceneDataDirty) to gate the 1 Hz iDate wall-clock refresh.
    bool didFullUploadOnce = false;
    bool sceneDataDirty = false;

    // ── Custom parameters / colors ─────────────────────────────────────
    float customParams[8][4] = {};
    float customColors[16][4] = {};

    // ── Resolved channel / texture sizes (node resolves live) ──────────
    /// iChannelResolution[i].xy — multipass buffer-channel output sizes.
    float channelResolution[4][2] = {};
    /// iTextureResolution[i].xy — user-texture slot sizes.
    float textureResolution[4][2] = {};

    // ── Audio ──────────────────────────────────────────────────────────
    int audioSpectrumSize = 0;

    // ── NDC Y orientation (per render target) ──────────────────────────
    /// True when this draw should carry the NDC Y-flip: a Y-up-in-NDC backend
    /// (OpenGL, rhi->isYUpInNDC()) AND rendering direct to the window — the
    /// producer (ShaderNodeRhi::syncBaseUniforms) clears it when the item is
    /// captured into a texture render target (a ShaderEffectSource layer),
    /// where a flipped write would invert what the consumer samples. Drives
    /// the qt_Matrix Y-flip the profile folds into fill().
    bool yUpInNDC = false;

    // ── Surface-only fields (BaseUniformProfile ignores these) ─────────
    /// Per-surface opacity from the Qt scene graph.
    float qtOpacity = 1.0f;
    /// Logical→device pixel scale for the decorated surface.
    float surfaceScale = 1.0f;
    /// 1.0 when the surface is focused, 0.0 otherwise.
    float surfaceFocused = 0.0f;
    /// Decorated surface size in device px.
    float surfaceSize[2] = {};
    /// Frame top-left in device px.
    float surfaceFrameTopLeft[2] = {};
    /// Frame size in device px.
    float surfaceFrameSize[2] = {};
};

/// Pluggable UBO concern for the shared shader render engine.
///
/// ShaderNodeRhi owns the SRB/pipeline/multipass/binding machinery; the ONLY
/// part that varies between the overlay/animation runtime and a future
/// surface-decoration runtime is the uniform buffer: its struct, byte size,
/// per-frame fill, and dirty-upload region dispatch. This interface abstracts
/// exactly that seam — bindings stay unabstracted because the surface binding
/// map (UBO@0, iChannel0-3@2-5, uTexture0@7) is a strict subset of the overlay
/// map.
///
/// Implementations hold their concrete UBO POD struct and expose its bytes via
/// data()/mutableData() so the node can drive updateDynamicBuffer directly.
class PHOSPHORSHADERS_EXPORT IUboProfile
{
public:
    virtual ~IUboProfile() = default;

    /// Byte size of the base UBO region (sizeof the concrete struct). Drives
    /// the node's UBO allocation and the IUniformExtension offset.
    virtual int baseSize() const = 0;

    /// Read-only view of the filled UBO bytes (for updateDynamicBuffer).
    virtual const void* data() const = 0;
    /// Mutable view — used by the node's multipass qt_Matrix pin/restore,
    /// which writes the leading mat4 directly at offset 0.
    virtual void* mutableData() = 0;

    /// Populate the UBO from a per-frame node snapshot. Implementations read
    /// only the subset of @p state they need.
    virtual void fill(const UboFrameState& state) = 0;

    /// Region(s) covering the whole base struct, for the first (full) upload.
    virtual UboUploadRegionList fullUploadRegions() const
    {
        UboUploadRegionList regions;
        regions.push(UboUploadRegion{0, baseSize()});
        return regions;
    }

    /// Region(s) to upload for the given dirty flags, reproducing the legacy
    /// broader-subsumes-narrower dispatch. Returns a fixed-capacity list —
    /// this runs per dirty frame and must not allocate.
    virtual UboUploadRegionList dirtyRegions(const UboDirtyFlags& flags) const = 0;

    /// Whether this profile exposes the two consumer escape-hatch int slots.
    virtual bool hasAppFields() const
    {
        return false;
    }
    /// Write the consumer's escape-hatch int slots. No-op by default.
    virtual void setAppField0(int /*value*/)
    {
    }
    virtual void setAppField1(int /*value*/)
    {
    }
};

} // namespace PhosphorShaders

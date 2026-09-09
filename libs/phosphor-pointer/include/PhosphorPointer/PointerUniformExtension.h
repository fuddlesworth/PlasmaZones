// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerShaderUniforms.h>
#include <PhosphorPointer/phosphorpointer_export.h>

#include <PhosphorShaders/IUniformExtension.h>

#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QVector2D>
#include <QVector4D>

#include <atomic>
#include <span>

namespace PhosphorPointerShaders {

/// IUniformExtension that appends `PointerUniformsTail` after
/// `PhosphorShaders::BaseUniforms` in the preview runtime's UBO. Modelled on
/// `PhosphorAnimation::AnimationUniformExtension`: a mutex serialises the
/// GUI-thread setters against the render thread's `write`, and an atomic
/// dirty flag tells the node when to re-upload the tail.
///
/// `requiresPhysicalResolution()` keeps the default (true): the pointer
/// canvas is the output in device px, so `iResolution` must be device px too.
class PHOSPHORPOINTER_EXPORT PointerUniformExtension : public PhosphorShaders::IUniformExtension
{
public:
    PointerUniformExtension();

    int extensionSize() const override;
    void write(char* buffer, int offset) const override;
    bool isDirty() const override;
    void clearDirty() override;

    /// `uPointerVelocity`: .xy = @p velocity in device px/s, .z its length.
    void setVelocity(const QVector2D& velocity);

    /// `uPointerPress`: canvas px, seconds since the press, button code.
    void setPress(const QPointF& pos, double secondsSince, int button);

    /// `uPointerRelease`: same shape as `setPress`.
    void setRelease(const QPointF& pos, double secondsSince, int button);

    /// `uPointerState.xyz`: pressed-button bitmask, seconds since the last
    /// motion, logical-to-device scale. `.w` (trail count) is owned by
    /// `setTrail`.
    void setState(int buttonsMask, double idleSeconds, double scale);

    /// `uCursorRect` in canvas px.
    void setCursorRect(const QRectF& rect);

    /// `uPointerFlags.x`. Leaves `.y` (the reach) alone.
    void setHasCursorSprite(bool has);

    /// The pack's resolved reach in LOGICAL px, as `resolvedReach()` gives
    /// it. Held here rather than passed per frame because it changes with the
    /// pack or its parameters, not with the pointer; `apply` multiplies it by
    /// the frame's scale on the way to `uPointerFlags.y`.
    void setReachLogicalPx(double reach);

    /// `uPointerTrail` (truncated to `kMaxTrailPoints`, the rest zeroed) and
    /// `uPointerState.w` = the count actually filled. @p trail is newest
    /// first, as `PointerFrameState::trail` holds it.
    void setTrail(std::span<const QVector4D> trail);

    /// Push a whole frame at once. Atomic with respect to `write`: the mutex
    /// is held for the whole frame, so the render thread copies either the
    /// previous frame's tail or this one, never a mix of the two.
    void apply(const PointerFrameState& state);

private:
    // The *Locked helpers carry the setter bodies and expect m_mutex held.
    // The public setters lock around one of them; apply() locks once around
    // all of them.
    void setVec4Locked(float (&dst)[4], float x, float y, float z, float w);
    void setVelocityLocked(const QVector2D& velocity);
    void setPressLocked(const QPointF& pos, double secondsSince, int button);
    void setReleaseLocked(const QPointF& pos, double secondsSince, int button);
    void setStateLocked(int buttonsMask, double idleSeconds, double scale);
    void setCursorRectLocked(const QRectF& rect);
    void setFlagsLocked(bool hasSprite, double scale);
    void setTrailLocked(std::span<const QVector4D> trail);

    PointerUniformsTail m_data;
    double m_reachLogicalPx = 0.0;
    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{true};
};

} // namespace PhosphorPointerShaders

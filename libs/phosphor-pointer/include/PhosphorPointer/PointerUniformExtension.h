// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerShaderUniforms.h>
#include <PhosphorPointer/phosphorpointer_export.h>

#include <PhosphorShaders/IUniformExtension.h>

#include <QList>
#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QVector2D>
#include <QVector4D>

#include <atomic>

namespace PhosphorPointerShaders {

/// Plain-data snapshot of everything the pointer contract's tail carries for
/// one frame. `PointerHistory::frameState` produces it, and both hosts (the
/// compositor pushing loose uniforms and the preview driving the UBO through
/// `PointerUniformExtension::apply`) consume the same type. Positions are in
/// canvas px (device px, top-down, origin at the output's top-left).
struct PHOSPHORPOINTER_EXPORT PointerFrameState
{
    /// Pointer velocity in device px/s.
    QVector2D velocity;

    /// Last press: position, seconds since (1e6 when none), button code
    /// (1 left, 2 right, 3 middle, 0 none).
    QPointF pressPos;
    double pressSecondsSince = 1.0e6;
    int pressButton = 0;

    /// Last release, same shape.
    QPointF releasePos;
    double releaseSecondsSince = 1.0e6;
    int releaseButton = 0;

    /// Pressed-button bitmask (1 left, 2 right, 4 middle).
    int buttons = 0;

    /// Seconds since the last motion (1e6 when none).
    double idleSeconds = 1.0e6;

    /// Logical-to-device scale of the canvas.
    double scale = 1.0;

    /// Cursor sprite rect in canvas px, hotspot applied. Null when unknown.
    QRectF cursorRect;

    /// Whether `uCursorSprite` is bound this frame.
    bool hasSprite = false;

    /// Trail samples, newest first, at most `kMaxTrailPoints`: `.xy` canvas
    /// px, `.z` age seconds, `.w` speed at the sample (device px/s).
    QList<QVector4D> trail;
};

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

    /// `uPointerFlags.x`.
    void setHasCursorSprite(bool has);

    /// `uPointerTrail` (truncated to `kMaxTrailPoints`, the rest zeroed) and
    /// `uPointerState.w` = the count actually filled.
    void setTrail(const QList<QVector4D>& trail);

    /// Push a whole frame at once.
    void apply(const PointerFrameState& state);

private:
    void setVec4Locked(float (&dst)[4], float x, float y, float z, float w);

    PointerUniformsTail m_data;
    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{true};
};

} // namespace PhosphorPointerShaders

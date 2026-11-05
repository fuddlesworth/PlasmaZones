// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/phosphorpointer_export.h>

#include <QPointF>
#include <QRectF>
#include <QVector2D>
#include <QVector4D>

#include <array>

namespace PhosphorPointerShaders {

/// Plain-data snapshot of everything the pointer contract's tail carries for
/// one frame. `PointerHistory::frameState` produces it, and both hosts (the
/// compositor pushing loose uniforms and the preview driving the UBO through
/// `PointerUniformExtension::apply`) consume the same type. Positions are in
/// canvas px (device px, top-down, origin at the output's top-left).
///
/// It lives in its own header so a sampler consumer that only needs the
/// frame shape does not pull the UBO extension's mutex and atomics along.
struct PHOSPHORPOINTER_EXPORT PointerFrameState
{
    /// Pointer velocity in device px/s.
    QVector2D velocity;

    /// Pointer speed with the per-sample jitter filtered out, in device px/s:
    /// an exponential filter walked over the current stroke's samples, oldest
    /// to newest (see `PointerHistory::filteredSpeed`). Pushed as
    /// `uPointerVelocity.w`, which is what `pointerFilteredSpeed()` in the
    /// shared shader library reads, so the speed a pack gates on is computed
    /// once per frame by the sampler that owns the ring rather than by every
    /// fragment.
    double filteredSpeed = 0.0;

    /// Last press: position, seconds since (`kNeverSeconds` when none),
    /// button code (1 left, 2 right, 3 middle, 0 none).
    QPointF pressPos;
    double pressSecondsSince = PointerShaderContract::kNeverSeconds;
    int pressButton = 0;

    /// Last release, same shape.
    QPointF releasePos;
    double releaseSecondsSince = PointerShaderContract::kNeverSeconds;
    int releaseButton = 0;

    /// Pressed-button bitmask (1 left, 2 right, 4 middle).
    int buttons = 0;

    /// Seconds since the last motion (`kNeverSeconds` when none).
    double idleSeconds = PointerShaderContract::kNeverSeconds;

    /// Logical-to-device scale of the canvas.
    double scale = 1.0;

    /// Cursor sprite rect in canvas px, hotspot applied. Null when unknown.
    QRectF cursorRect;

    /// Whether `uCursorSprite` is bound this frame.
    bool hasSprite = false;

    /// Trail samples, newest first: `.xy` canvas px, `.z` age seconds, `.w`
    /// speed at the sample (device px/s). Only the first `trailCount` entries
    /// are meaningful and the rest are zero. Fixed storage rather than a
    /// list because a frame state is built once per painted frame on the
    /// compositor path, and the ring never holds more than the contract's
    /// capacity anyway.
    std::array<QVector4D, PointerShaderContract::kMaxTrailPoints> trail{};
    int trailCount = 0;

    [[nodiscard]] bool trailIsEmpty() const
    {
        return trailCount <= 0;
    }
    [[nodiscard]] int trailSize() const
    {
        return trailCount;
    }
    /// The newest sample, which is the pointer itself. Only meaningful when
    /// `!trailIsEmpty()`; on an empty trail it is the zero vector.
    [[nodiscard]] const QVector4D& newestTrail() const
    {
        return trail[0];
    }
    [[nodiscard]] const QVector4D& trailAt(int index) const
    {
        return trail[static_cast<size_t>(index)];
    }
};

} // namespace PhosphorPointerShaders

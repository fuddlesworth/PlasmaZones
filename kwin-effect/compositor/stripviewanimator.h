// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>

#include <QtGlobal>

#include <functional>
#include <memory>
#include <unordered_map>

namespace KWin {
class LogicalOutput;
}

namespace PhosphorAnimation {
class Curve;
}

namespace PlasmaZones {

/**
 * @brief Scrolling-strip view motion, one spring per output.
 *
 * A scroll moves every column on the strip by the same amount. Committing that
 * as N geometry changes makes WindowAnimator start N independent springs, each
 * beginning a moment apart and integrating separately, so the strip shears
 * instead of sliding. This class springs the VIEW once per output; the paint
 * path adds its offset to every carried window, and the strip moves as one
 * object.
 *
 * @par What is animated
 * The absolute view coordinate, not the offset. That distinction is what lets
 * a second scroll arriving mid-flight be an ordinary `retarget()`: the target
 * becomes the new committed view while the animated position stays continuous
 * and `RetargetPolicy::PreserveVelocity` carries the momentum across. Animating
 * the OFFSET instead would need the value itself to jump every time committed
 * geometry moved out from under it, which is not a retarget and would need a
 * velocity-seeded start the library does not offer.
 *
 * The coordinate's ORIGIN is arbitrary — it is an accumulation of the deltas
 * the engine has sent for this output, and only differences are ever read. It
 * deliberately does not try to mirror the engine's own `viewX`: the two would
 * have to be kept in step across every context switch, screen change and
 * restore, and nothing needs them to agree.
 *
 * @par Sign
 * `TileRequestEntry::viewDeltaX` is the translation that puts a window back
 * where it was rendered. Committed view advances BY that delta, and the paint
 * offset is `committed - animated`, which therefore starts at the delta and
 * rings out to zero.
 */
class StripViewAnimator
{
public:
    StripViewAnimator() = default;
    ~StripViewAnimator() = default;

    StripViewAnimator(const StripViewAnimator&) = delete;
    StripViewAnimator& operator=(const StripViewAnimator&) = delete;

    using OutputClockResolver = std::function<PhosphorAnimation::IMotionClock*(KWin::LogicalOutput*)>;
    using RepaintRequest = std::function<void(KWin::LogicalOutput*)>;

    void setOutputClockResolver(OutputClockResolver resolver);
    /// Damage hook, called for an output whose view is in flight. The whole
    /// strip moves, so this is deliberately per-OUTPUT rather than per-window:
    /// there is no useful smaller region while every column is sliding.
    void setRepaintRequest(RepaintRequest request);

    void setEnabled(bool enabled);
    bool isEnabled() const
    {
        return m_enabled;
    }
    void setCurve(std::shared_ptr<const PhosphorAnimation::Curve> curve);
    void setDuration(qreal ms);

    /// Largest view delta honoured, in logical pixels. A batch cannot slide the
    /// strip further than this in one leg. The wire deliberately does not
    /// validate the field (it is a motion hint, and rejecting a whole tile
    /// request over a cosmetic value would drop a valid placement), so the
    /// clamp lives here instead. Generous enough for any real strip on any
    /// real monitor wall, finite enough that a garbled value cannot fling the
    /// strip somewhere it takes seconds to spring back from.
    static constexpr int kMaxViewDeltaPx = 100000;

    /// Fold one batch's view delta into @p output's spring. A no-op for a zero
    /// delta, so callers need not pre-filter.
    void applyBatchDelta(KWin::LogicalOutput* output, int deltaX);

    /// Paint translation for a window carried by @p output's view, in logical
    /// pixels. Zero when nothing is in flight, which is the resting state and
    /// the common case.
    qreal offsetFor(KWin::LogicalOutput* output) const;

    bool hasActiveAnimations() const;
    bool isAnimatingOn(KWin::LogicalOutput* output) const;

    /// Drop @p output's state entirely — for a disconnect, where the spring
    /// and the accumulated view both stop describing anything real. The next
    /// batch for a re-connected output starts a fresh accumulation.
    void forgetOutput(KWin::LogicalOutput* output);
    int reapAnimationsForClock(const PhosphorAnimation::IMotionClock* clock);

    void advanceAnimations();
    void scheduleRepaints() const;

private:
    struct ViewMotion
    {
        /// Where the strip's committed geometry currently sits, in this
        /// output's accumulated view coordinate.
        qreal committed = 0.0;
        PhosphorAnimation::AnimatedValue<qreal> animation;
    };

    PhosphorAnimation::IMotionClock* clockForOutput(KWin::LogicalOutput* output) const;

    std::unordered_map<KWin::LogicalOutput*, ViewMotion> m_motions;
    PhosphorAnimation::Profile m_profile;
    OutputClockResolver m_outputClockResolver;
    RepaintRequest m_repaintRequest;
    bool m_enabled = true;
};

} // namespace PlasmaZones

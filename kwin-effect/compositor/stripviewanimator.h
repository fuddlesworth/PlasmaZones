// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>

#include <QPointF>
#include <QtGlobal>

#include <functional>
#include <unordered_map>

namespace KWin {
class LogicalOutput;
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
 * deliberately does not try to mirror the engine's own `viewOffset`: the two would
 * have to be kept in step across every context switch, screen change and
 * restore, and nothing needs them to agree.
 *
 * @par Sign
 * `TileRequestEntry::viewDelta` is the translation that puts a window back
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
    ///
    /// @p profile is resolved per batch by the caller rather than configured on
    /// this object, so the `scrolling.view` motion node reaches the strip the
    /// same way every other event's node reaches its animation. It already
    /// carries the global curve and duration as its base, so there is nothing
    /// left for this class to hold.
    ///
    /// A retarget deliberately keeps the IN-FLIGHT leg's profile: swapping
    /// curves under a moving spring would discard its velocity, and a scroll
    /// arriving mid-leg is the case that most needs the momentum kept.
    ///
    /// Returns true when a leg actually STARTED or RETARGETED. False when
    /// the delta was folded into the accumulator with no leg — animations
    /// disabled, no clock for the output (hotplug race), zero delta, null
    /// output. Callers that build state assuming a moving spring (the
    /// residual origins, the strip shader pass, the cascade decision) must
    /// gate on this rather than on the wire delta: with no leg the paint
    /// path's offset is zero, so an origin placed a delta behind the target
    /// pops backwards and slides double.
    /// @p deltaIn is a SIGNED SCALAR along @p axis, that output's strip axis.
    ///
    /// The animated value stays one-dimensional on purpose. kMaxViewDeltaPx is
    /// a scalar budget the design leans on twice (the wire boundary clamps to
    /// it and this class re-clamps idempotently), and a QPointF would admit a
    /// diagonal at sqrt(2) times it while carrying a velocity vector whose
    /// off-axis component is provably but no longer structurally zero.
    ///
    /// An axis MISMATCH against a live leg cancels rather than retargets: the
    /// in-flight motion describes travel along an axis the strip no longer
    /// has, so there is nothing to preserve velocity through.
    bool applyBatchDelta(KWin::LogicalOutput* output, int deltaIn, PhosphorProtocol::ScrollAxis axis,
                         const PhosphorAnimation::Profile& profile);

    /// Fold one batch's view delta into @p output's accumulator WITHOUT a leg
    /// — for user-driven continuous view motion (the drag edge auto-scroll
    /// heartbeat), where the ~60 Hz commits are the motion and a leg
    /// retargeted every tick never progresses on a stateless curve. Any leg
    /// in flight is cancelled (with a repaint, since its offset vanishes and
    /// nothing else repaints it away), so the strip lands exactly on the
    /// committed geometry. The accumulator still moves so a later discrete
    /// scroll's leg starts from the true committed view.
    void applyImmediateDelta(KWin::LogicalOutput* output, int deltaIn, PhosphorProtocol::ScrollAxis axis);

    /// Paint translation for a window carried by @p output's view, in logical
    /// pixels, already resolved onto that output's own strip axis so a caller
    /// cannot put it in the wrong component. A null point when nothing is in
    /// flight, which is the resting state and the common case.
    QPointF offsetFor(KWin::LogicalOutput* output) const;

    /// The same value as a signed scalar along that axis, for the shader pass
    /// and the motion sampler, which stay one-dimensional by design.
    qreal offsetAlongAxis(KWin::LogicalOutput* output) const;

    /// Which axis that scalar runs along. Horizontal for an output this class
    /// has never seen — the historical layout, and the only safe answer when
    /// no batch has named the axis yet.
    ///
    /// Exists so a consumer holding an OUTPUT (the shader pass) can ask
    /// without first mapping back to a screen id: the effect's own map runs
    /// screenId -> output, so the reverse lookup would be a second, weaker
    /// source of the same fact.
    PhosphorProtocol::ScrollAxis axisFor(KWin::LogicalOutput* output) const;

    bool hasActiveAnimations() const;
    bool isAnimatingOn(KWin::LogicalOutput* output) const;

    /// Monotonic counter of view deltas this class has been handed, bumped by
    /// BOTH apply entry points for every non-zero delta on a real output —
    /// before either can decline to start a leg.
    ///
    /// It exists because "the strip is moving" and "a spring is in flight" are
    /// NOT the same question, and only this counter answers the first one.
    /// applyBatchDelta returns false without ever animating when the master
    /// animation toggle is off or the output has no clock, and
    /// applyImmediateDelta (the drag edge auto-scroll heartbeat) cancels any
    /// leg and deliberately never starts one — so hasActiveAnimations() reads
    /// false through an entire fast scroll for those users and those paths. A
    /// consumer that wants "has the user been driving the strip recently"
    /// must watch this change and stamp its own clock, not poll the springs.
    quint64 viewMotionGeneration() const
    {
        return m_viewMotionGeneration;
    }

    /// Drop @p output's state entirely — for a disconnect, where the spring
    /// and the accumulated view both stop describing anything real. The next
    /// batch for a re-connected output starts a fresh accumulation.
    void forgetOutput(KWin::LogicalOutput* output);

    /// Drop every output's accumulator and leg, WITHOUT touching the enable
    /// flag (unlike setEnabled(false), which is the master-toggle path).
    /// For session-scoped teardown — daemon loss clears the scrolling set,
    /// so every spring belongs to a strip that no longer exists. Schedules
    /// repaints for the dropped legs first, since nothing else will paint
    /// their offsets away.
    void reset();
    /// Drop every LIVE leg driven by @p clock (a dying output's clock).
    /// NOT a complete teardown on its own: an accumulator-only entry (one
    /// applyImmediateDelta created with no leg to reap) is invisible to the
    /// isAnimating() test and is forgetOutput's responsibility — which
    /// screenRemoved calls unconditionally before this, so nothing leaks.
    int reapAnimationsForClock(const PhosphorAnimation::IMotionClock* clock);

    void advanceAnimations();

private:
    /// Damage every output with a live leg.
    ///
    /// Private, and deliberately not a per-frame driver the way the window
    /// animator's namesake is: an in-flight AnimatedValue calls
    /// clock->requestFrame() on every tick, and CompositorClock turns that
    /// into an addRepaint for the output, so the leg pumps its own frames.
    /// This exists for the paths that drop a leg without advancing it, where
    /// the last presented frame still carries an offset nothing else will
    /// repaint away.
    void scheduleRepaints() const;

    struct ViewMotion
    {
        /// Where the strip's committed geometry currently sits, in this
        /// output's accumulated view coordinate.
        qreal committed = 0.0;
        /// Which axis that coordinate runs along. Held per output because a
        /// portrait and a landscape monitor coexist, and because a leg has to
        /// be able to tell an axis FLIP from an ordinary retarget.
        PhosphorProtocol::ScrollAxis axis = PhosphorProtocol::ScrollAxis::Horizontal;
        PhosphorAnimation::AnimatedValue<qreal> animation;
    };

    PhosphorAnimation::IMotionClock* clockForOutput(KWin::LogicalOutput* output) const;

    std::unordered_map<KWin::LogicalOutput*, ViewMotion> m_motions;
    OutputClockResolver m_outputClockResolver;
    RepaintRequest m_repaintRequest;
    /// See viewMotionGeneration(). Never reset — it means "a delta was
    /// applied", so reset()/forgetOutput()/setEnabled() leave it alone: a
    /// consumer compares it against its own last-seen value, and rewinding it
    /// would make a genuine motion read as none.
    quint64 m_viewMotionGeneration = 0;
    bool m_enabled = true;
};

} // namespace PlasmaZones

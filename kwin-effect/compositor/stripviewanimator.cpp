// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stripviewanimator.h"

#include <PhosphorAnimation/Curve.h>

#include <utility>

namespace PlasmaZones {

void StripViewAnimator::setOutputClockResolver(OutputClockResolver resolver)
{
    m_outputClockResolver = std::move(resolver);
}

void StripViewAnimator::setRepaintRequest(RepaintRequest request)
{
    m_repaintRequest = std::move(request);
}

void StripViewAnimator::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled && !m_motions.empty()) {
        // Drop in-flight legs on disable, the same reasoning as
        // WindowAnimator's: advanceAnimations has no enabled gate, so a leg
        // from before the toggle would keep offsetting the strip while the
        // apply path has already committed the final geometry outright. Damage
        // every affected output first — the last painted frame is offset, and
        // nothing else will repaint it.
        scheduleRepaints();
        m_motions.clear();
    }
}

bool StripViewAnimator::applyBatchDelta(KWin::LogicalOutput* output, int delta_, PhosphorProtocol::ScrollAxis axis,
                                        const PhosphorAnimation::Profile& profile)
{
    if (!output || delta_ == 0) {
        return false;
    }
    const qreal delta = qBound(-kMaxViewDeltaPx, delta_, kMaxViewDeltaPx);

    ViewMotion& motion = m_motions[output];
    if (motion.axis != axis) {
        // The strip's axis flipped under a live leg. CANCEL rather than
        // retarget: the in-flight motion describes travel along an axis this
        // output no longer has, so there is no velocity worth preserving and
        // retargeting would carry sideways momentum into a vertical slide.
        //
        // Damage first, for the same reason the no-clock arm below does: the
        // offset the leg was contributing vanishes with the cancel, and the
        // committed geometry is already final, so nothing else would repaint
        // the stale frame away.
        //
        // The accumulator resets too. It is an accumulated coordinate ALONG an
        // axis, so carrying it across a flip would leave the new axis's first
        // leg starting from a distance measured on the old one.
        if (motion.animation.isAnimating() && m_repaintRequest) {
            m_repaintRequest(output);
        }
        motion.animation.cancel();
        motion.committed = 0.0;
        motion.axis = axis;
    }
    const qreal previousCommitted = motion.committed;
    motion.committed += delta;

    if (!m_enabled) {
        // Animations off: the strip jumps to the committed positions, which is
        // what the apply path has already done. The accumulator still moves,
        // but only for bookkeeping symmetry within this disabled episode —
        // setEnabled(false) clears m_motions outright, so nothing survives the
        // toggle itself. That costs nothing either way: offsetFor only ever
        // reads committed MINUS the animated value, so the absolute origin is
        // arbitrary and only differences are observable.
        motion.animation.cancel();
        return false;
    }

    PhosphorAnimation::IMotionClock* clock = clockForOutput(output);
    if (!clock) {
        // No clock for this output (hotplug race, test harness). Nothing can
        // drive a leg, and an un-advanced animation would freeze the strip at
        // a permanent offset, so leave the view at rest on the committed
        // geometry.
        //
        // Damage first if a leg was live: the offset it was contributing
        // vanishes with the cancel, and nothing else will repaint it away.
        // advance() was the frame pump, and the columns' committed geometry is
        // already final, so KWin sees no reason of its own to redraw — the last
        // presented frame would keep the stale offset until unrelated damage
        // happened along. setEnabled(false) handles the same case the same way.
        if (motion.animation.isAnimating() && m_repaintRequest) {
            m_repaintRequest(output);
        }
        motion.animation.cancel();
        return false;
    }

    if (motion.animation.isAnimating()) {
        // The strip is already sliding and has now been told to slide further.
        // Because the value is the ABSOLUTE view rather than the offset, this
        // is an ordinary retarget: the animated position stays exactly where
        // it is (so the frame on screen does not jump) while the target moves
        // to the new committed view, and PreserveVelocity carries the
        // in-flight momentum into the new leg. This is the held-arrow-key and
        // repeated-wheel-tick case.
        motion.animation.retarget(motion.committed, PhosphorAnimation::RetargetPolicy::PreserveVelocity);
    } else {
        PhosphorAnimation::MotionSpec<qreal> spec;
        spec.profile = profile;
        spec.clock = clock;
        spec.retargetPolicy = PhosphorAnimation::RetargetPolicy::PreserveVelocity;
        // Start where the strip was RENDERED, not where it now sits: the
        // committed geometry has already moved by the delta, so the leg has to
        // begin a delta behind it and catch up.
        motion.animation.start(previousCommitted, motion.committed, spec);
    }

    if (m_repaintRequest) {
        m_repaintRequest(output);
    }
    return true;
}

qreal StripViewAnimator::offsetAlongAxis(KWin::LogicalOutput* output) const
{
    if (!output) {
        return 0.0;
    }
    const auto it = m_motions.find(output);
    if (it == m_motions.end() || !it->second.animation.isAnimating()) {
        return 0.0;
    }
    return it->second.committed - it->second.animation.value();
}

QPointF StripViewAnimator::offsetFor(KWin::LogicalOutput* output) const
{
    const qreal along = offsetAlongAxis(output);
    if (qFuzzyIsNull(along)) {
        return {};
    }
    const auto it = m_motions.find(output);
    const PhosphorProtocol::ScrollAxis axis =
        it == m_motions.end() ? PhosphorProtocol::ScrollAxis::Horizontal : it->second.axis;
    // Resolved HERE rather than at each paint site, so a caller cannot put the
    // scalar in the wrong component — which is the whole failure this returns
    // a point to prevent.
    return axis == PhosphorProtocol::ScrollAxis::Horizontal ? QPointF(along, 0.0) : QPointF(0.0, along);
}

bool StripViewAnimator::hasActiveAnimations() const
{
    for (const auto& [output, motion] : m_motions) {
        if (motion.animation.isAnimating()) {
            return true;
        }
    }
    return false;
}

bool StripViewAnimator::isAnimatingOn(KWin::LogicalOutput* output) const
{
    if (!output) {
        return false;
    }
    const auto it = m_motions.find(output);
    return it != m_motions.end() && it->second.animation.isAnimating();
}

void StripViewAnimator::forgetOutput(KWin::LogicalOutput* output)
{
    m_motions.erase(output);
}

void StripViewAnimator::reset()
{
    // Repaint every output with a live leg BEFORE dropping it: the offset
    // each was contributing vanishes with the erase and nothing else will
    // repaint it away (same reasoning as the no-clock arm of
    // applyBatchDelta). The enable flag is deliberately untouched — this is
    // the session-teardown path, not the master toggle.
    scheduleRepaints();
    m_motions.clear();
}

int StripViewAnimator::reapAnimationsForClock(const PhosphorAnimation::IMotionClock* clock)
{
    if (!clock) {
        return 0;
    }
    int reaped = 0;
    for (auto it = m_motions.begin(); it != m_motions.end();) {
        if (it->second.animation.isAnimating() && it->second.animation.spec().clock == clock) {
            // The whole entry goes, not just the animation: the clock dying
            // means the output did, and the accumulated view describes a strip
            // that is no longer on any screen.
            if (m_repaintRequest) {
                m_repaintRequest(it->first);
            }
            it = m_motions.erase(it);
            ++reaped;
        } else {
            ++it;
        }
    }
    return reaped;
}

void StripViewAnimator::advanceAnimations()
{
    for (auto& [output, motion] : m_motions) {
        if (!motion.animation.isAnimating()) {
            continue;
        }
        // Per-tick clock re-resolution, mirroring WindowAnimator: an output
        // whose clock was rebuilt (mode change, hotplug) must not keep
        // stepping against the old one.
        if (PhosphorAnimation::IMotionClock* resolved = clockForOutput(output)) {
            PhosphorAnimation::IMotionClock* current = motion.animation.spec().clock;
            if (resolved != current && PhosphorAnimation::IMotionClock::epochCompatible(current, resolved)) {
                motion.animation.rebindClock(resolved);
            }
        }
        motion.animation.advance();
        if (!motion.animation.isAnimating()) {
            // Settling frame: the offset has just become zero, and the last
            // frame drawn still carries the old one.
            if (m_repaintRequest) {
                m_repaintRequest(output);
            }
        }
    }
}

void StripViewAnimator::scheduleRepaints() const
{
    if (!m_repaintRequest) {
        return;
    }
    for (const auto& [output, motion] : m_motions) {
        if (motion.animation.isAnimating()) {
            m_repaintRequest(output);
        }
    }
}

PhosphorAnimation::IMotionClock* StripViewAnimator::clockForOutput(KWin::LogicalOutput* output) const
{
    return m_outputClockResolver ? m_outputClockResolver(output) : nullptr;
}

} // namespace PlasmaZones

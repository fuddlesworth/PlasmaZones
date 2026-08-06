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

void StripViewAnimator::setSettleCallback(RepaintRequest callback)
{
    m_settleCallback = std::move(callback);
}

void StripViewAnimator::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled && !m_motions.empty()) {
        // Every in-flight leg is about to be dropped, and a dropped leg is
        // still a strip coming to rest as far as anyone waiting is concerned.
        if (m_settleCallback) {
            for (const auto& [output, motion] : m_motions) {
                if (motion.animation.isAnimating()) {
                    m_settleCallback(output);
                }
            }
        }
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

void StripViewAnimator::applyBatchDelta(KWin::LogicalOutput* output, int deltaX,
                                        const PhosphorAnimation::Profile& profile)
{
    if (!output || deltaX == 0) {
        return;
    }
    const qreal delta = qBound(-kMaxViewDeltaPx, deltaX, kMaxViewDeltaPx);

    ViewMotion& motion = m_motions[output];
    const qreal previousCommitted = motion.committed;
    motion.committed += delta;

    if (!m_enabled) {
        // Animations off: the strip jumps to the committed positions, which is
        // what the apply path has already done. Keep the accumulator moving so
        // a later re-enable starts from a truthful baseline rather than
        // springing away an offset that was never on screen.
        motion.animation.cancel();
        return;
    }

    PhosphorAnimation::IMotionClock* clock = clockForOutput(output);
    if (!clock) {
        // No clock for this output (hotplug race, test harness). Nothing can
        // drive a leg, and an un-advanced animation would freeze the strip at
        // a permanent offset, so leave the view at rest on the committed
        // geometry.
        motion.animation.cancel();
        return;
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
}

qreal StripViewAnimator::offsetFor(KWin::LogicalOutput* output) const
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
            // Reaped, not finished — but the strip is equally at rest, and a
            // consumer waiting on a leg whose output just went away would wait
            // forever otherwise.
            if (m_settleCallback) {
                m_settleCallback(it->first);
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
            // The strip is at rest. This edge is the only honest answer to
            // "when did the scroll finish" — a spring ignores its profile's
            // duration and runs on its own physics, so nothing outside this
            // loop can compute the moment.
            if (m_settleCallback) {
                m_settleCallback(output);
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

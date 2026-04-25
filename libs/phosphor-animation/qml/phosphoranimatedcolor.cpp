// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedColor.h>

#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

#include <QLoggingCategory>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcAnimatedColor, "phosphoranimation.qml.color")
}

PhosphorAnimatedColor::PhosphorAnimatedColor(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedColor::~PhosphorAnimatedColor()
{
    // Cancel BOTH underlying AnimatedValue instances first, THEN let
    // the default-destruction path run. See
    // PhosphorAnimatedReal::~PhosphorAnimatedReal for the UAF
    // rationale — Color has two instances because `colorSpace`
    // selects between a Linear-space and an OkLab-space core, so
    // both need the pre-destruction cancel.
    m_animatedValueLinear.cancel();
    m_animatedValueOkLab.cancel();
}

QColor PhosphorAnimatedColor::activeFrom() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.from() : m_animatedValueLinear.from();
}

QColor PhosphorAnimatedColor::activeTo() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.to() : m_animatedValueLinear.to();
}

QColor PhosphorAnimatedColor::activeValue() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.value() : m_animatedValueLinear.value();
}

// Dispatch accessor reading from whichever space is active. The two
// AnimatedValue<QColor> instances are independent — writes to one do
// not propagate to the other — but `setColorSpace` calls
// `AnimatedValue::seedFrom` on the target instance before flipping
// `m_activeSpace`, so the idle-state endpoints and current value stay
// continuous across a space flip.
QColor PhosphorAnimatedColor::from() const
{
    return activeFrom();
}
QColor PhosphorAnimatedColor::to() const
{
    return activeTo();
}
QColor PhosphorAnimatedColor::value() const
{
    return activeValue();
}

bool PhosphorAnimatedColor::isAnimating() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.isAnimating()
                                              : m_animatedValueLinear.isAnimating();
}
bool PhosphorAnimatedColor::isComplete() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.isComplete() : m_animatedValueLinear.isComplete();
}

PhosphorAnimatedColor::ColorSpace PhosphorAnimatedColor::colorSpace() const
{
    return m_activeSpace;
}

void PhosphorAnimatedColor::setColorSpace(ColorSpace space)
{
    if (space == m_activeSpace) {
        return;
    }
    if (isAnimating()) {
        // Flipping space mid-animation would produce a visible
        // chromatic-path jump. Refuse the write and warn — silent
        // ignore would desync any QML two-way binding (a Slider
        // bound to colorSpace that fires during a fade sees its
        // write dropped and read-back reports the old value, with
        // no diagnostic). The test
        // `testColorSpaceIgnoredWhileAnimating` pins the no-emit
        // contract on the rejected path: a refused write must NOT
        // fire `colorSpaceChanged`, otherwise observers think the
        // flip succeeded.
        qCWarning(lcAnimatedColor) << "setColorSpace" << static_cast<int>(space)
                                   << "ignored while animating — flip requires a quiesced animation "
                                      "(call after isComplete(), or retarget to the same value to quiesce)";
        return;
    }

    // Snapshot every reachable observable BEFORE the flip — including
    // `isAnimating()` and `isComplete()` for completeness — so the
    // diff-emit step fires only the per-property signals whose
    // post-flip read diverges from the pre-flip read. Matches the
    // project's "only emit when value actually changes" contract.
    // `runWithDiffEmit` handles the snapshot, op, and per-field
    // diff-emit. We then emit `colorSpaceChanged` LAST (outside the
    // helper) so observers reading `value` from a `colorSpaceChanged`
    // handler see the post-flip ground truth.
    //
    // Seeding is the actual flip work:
    //   `seedFrom` copies visible idle state (from/to/current/isComplete).
    //   `seedSpecFrom` copies the MotionSpec's clock + callbacks —
    //   required so `retarget()` on the target instance after the flip
    //   is NOT silently rejected by `AnimatedValue::retarget`'s
    //   `if (!m_spec.clock) return false;` guard. Without this, a flip
    //   between `start()` and a subsequent `retarget()` would look like
    //   the retarget call is a no-op at the wrapper boundary. Profile
    //   is deliberately left alone — profile belongs to the NEXT
    //   start(), not to the post-flip retarget continuation.
    detail::runWithDiffEmit(this, [this, space]() {
        if (space == ColorSpace::OkLab) {
            m_animatedValueOkLab.seedFrom(m_animatedValueLinear);
            m_animatedValueOkLab.seedSpecFrom(m_animatedValueLinear);
        } else {
            m_animatedValueLinear.seedFrom(m_animatedValueOkLab);
            m_animatedValueLinear.seedSpecFrom(m_animatedValueOkLab);
        }
        m_activeSpace = space;
    });

    // Emit the meta-state signal LAST so a QML binding reading
    // `value` from a `colorSpaceChanged` handler always sees the
    // post-flip ground truth. Reordering this above the
    // runWithDiffEmit diff loop would invert the ordering contract
    // the test enshrines.
    Q_EMIT colorSpaceChanged();
}

bool PhosphorAnimatedColor::start(const QColor& from, const QColor& to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedColor::startImpl(const QColor& from, const QColor& to, IMotionClock* clock)
{
    if (!clock) {
        return false;
    }
    // Build the spec once; the active-instance dispatch below routes
    // it to whichever AnimatedValue<QColor, Space> backs the current
    // colorSpace. Per-tick callbacks emit the wrapper's own signals.
    MotionSpec<QColor> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const QColor&) {
        Q_EMIT valueChanged();
    };
    spec.onComplete = [this]() {
        Q_EMIT animatingChanged();
        Q_EMIT completeChanged();
    };

    return detail::runWithDiffEmit(this, [this, &from, &to, spec = std::move(spec)]() mutable {
        return (m_activeSpace == ColorSpace::OkLab) ? m_animatedValueOkLab.start(from, to, std::move(spec))
                                                    : m_animatedValueLinear.start(from, to, std::move(spec));
    });
}

bool PhosphorAnimatedColor::retarget(const QColor& to)
{
    return detail::runWithDiffEmit(this, [this, &to]() {
        return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.retarget(to)
                                                  : m_animatedValueLinear.retarget(to);
    });
}

void PhosphorAnimatedColor::cancel()
{
    detail::runWithDiffEmit(this, [this]() {
        if (m_activeSpace == ColorSpace::OkLab) {
            m_animatedValueOkLab.cancel();
        } else {
            m_animatedValueLinear.cancel();
        }
    });
}

void PhosphorAnimatedColor::finish()
{
    // `AnimatedValue::finish()` fires the spec's `onValueChanged`
    // callback (installed in `startImpl`) which already emits
    // `valueChanged()` on `this`. Pass `emitValueChanged=false` to
    // `runWithDiffEmit` so the snapshot-diff path does NOT re-emit
    // `valueChanged` — that would double-tick the terminal-value
    // transition. animatingChanged / completeChanged stay enabled
    // because the idempotent no-op path (finish on an already-
    // complete AnimatedValue, where the internal callback doesn't
    // fire) still needs the correct edge signals — boolean-edge
    // signals are idempotent for QML bindings on repeated
    // same-value notifications.
    detail::runWithDiffEmit(
        this,
        [this]() {
            if (m_activeSpace == ColorSpace::OkLab) {
                m_animatedValueOkLab.finish();
            } else {
                m_animatedValueLinear.finish();
            }
        },
        /*emitValueChanged=*/false);
}

void PhosphorAnimatedColor::advance()
{
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.advance();
    } else {
        m_animatedValueLinear.advance();
    }
}

void PhosphorAnimatedColor::onSync()
{
    advance();
}

} // namespace PhosphorAnimation

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedColor.h>

#include <PhosphorAnimation/MotionSpec.h>

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
        // chromatic-path jump. Refuse the write and warn: a silent
        // ignore desyncs QML two-way bindings (a Slider bound to
        // colorSpace that fires during a fade sees its write
        // dropped and read-back reports the old value — user-visible
        // drift with no diagnostic). Warn rather than debug so the
        // caller sees the drop on the first occurrence.
        qCWarning(lcAnimatedColor) << "setColorSpace" << static_cast<int>(space)
                                   << "ignored while animating — flip requires a quiesced animation "
                                      "(call after isComplete(), or retarget to the same value to quiesce)";
        return;
    }

    // Seed the target-space instance with the outgoing instance's
    // idle state (from, to, current value, isComplete) so post-flip
    // reads through from()/to()/value()/isComplete() stay continuous.
    // Without this, start(red, blue) → wait idle → setColorSpace(OkLab)
    // would jump value() to the OkLab instance's default-constructed
    // QColor() — the two AnimatedValue<QColor, Space> instances are
    // otherwise independent. `seedFrom` is a no-op if either side is
    // animating; the isAnimating() guard above gates that case.
    //
    // Snapshot prev-read values so we can emit fromChanged /
    // toChanged / valueChanged only if the post-flip read differs
    // from the pre-flip read — matches the project's "only emit
    // when value actually changes" contract.
    const QColor prevFrom = from();
    const QColor prevTo = to();
    const QColor prevValue = value();

    // `seedFrom` copies visible idle state (from/to/current/isComplete).
    // `seedSpecFrom` copies the MotionSpec's clock + callbacks — required
    // so `retarget()` on the target instance after the flip is NOT
    // silently rejected by `AnimatedValue::retarget`'s
    // `if (!m_spec.clock) return false;` guard. Without this, a flip
    // between `start()` and a subsequent `retarget()` would look like
    // the retarget call is a no-op at the wrapper boundary. Profile is
    // deliberately left alone — profile belongs to the NEXT start(), not
    // to the post-flip retarget continuation.
    if (space == ColorSpace::OkLab) {
        m_animatedValueOkLab.seedFrom(m_animatedValueLinear);
        m_animatedValueOkLab.seedSpecFrom(m_animatedValueLinear);
    } else {
        m_animatedValueLinear.seedFrom(m_animatedValueOkLab);
        m_animatedValueLinear.seedSpecFrom(m_animatedValueOkLab);
    }

    m_activeSpace = space;
    Q_EMIT colorSpaceChanged();

    if (from() != prevFrom) {
        Q_EMIT fromChanged();
    }
    if (to() != prevTo) {
        Q_EMIT toChanged();
    }
    if (value() != prevValue) {
        Q_EMIT valueChanged();
    }
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
    // Check-before-emit via the dispatch accessors. Mirrors
    // PhosphorAnimatedReal::startImpl.
    const QColor prevFrom = this->from();
    const QColor prevTo = this->to();
    const QColor prevValue = this->value();
    const bool prevAnimating = isAnimating();
    const bool prevComplete = isComplete();

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

    const bool ok = (m_activeSpace == ColorSpace::OkLab) ? m_animatedValueOkLab.start(from, to, std::move(spec))
                                                         : m_animatedValueLinear.start(from, to, std::move(spec));

    if (this->from() != prevFrom)
        Q_EMIT fromChanged();
    if (this->to() != prevTo)
        Q_EMIT toChanged();
    if (this->value() != prevValue)
        Q_EMIT valueChanged();
    if (isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (isComplete() != prevComplete)
        Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedColor::retarget(const QColor& to)
{
    const QColor prevFrom = this->from();
    const QColor prevTo = this->to();
    const QColor prevValue = this->value();
    const bool prevAnimating = isAnimating();
    const bool prevComplete = isComplete();
    const bool ok =
        m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.retarget(to) : m_animatedValueLinear.retarget(to);
    if (this->from() != prevFrom)
        Q_EMIT fromChanged();
    if (this->to() != prevTo)
        Q_EMIT toChanged();
    if (this->value() != prevValue)
        Q_EMIT valueChanged();
    if (isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (isComplete() != prevComplete)
        Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedColor::cancel()
{
    const bool prevAnimating = isAnimating();
    const bool prevComplete = isComplete();
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.cancel();
    } else {
        m_animatedValueLinear.cancel();
    }
    if (isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (isComplete() != prevComplete)
        Q_EMIT completeChanged();
}

void PhosphorAnimatedColor::finish()
{
    const bool prevAnimating = isAnimating();
    const bool prevComplete = isComplete();
    // NOTE: we do NOT capture or diff-emit `valueChanged` here.
    // `AnimatedValue::finish()` fires the spec's `onValueChanged`
    // callback (installed in `startImpl` below) which already emits
    // `valueChanged()` on `this` — diff-emitting here would double-
    // tick the terminal-value transition. Kept animatingChanged /
    // completeChanged as diff emits so the idempotent no-op path
    // (finish on an already-complete AnimatedValue, where the internal
    // callback doesn't fire) still produces the correct edge signals.
    // Matches the `finishImpl` convention in
    // PhosphorAnimatedValueImpl.h.
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.finish();
    } else {
        m_animatedValueLinear.finish();
    }
    if (isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (isComplete() != prevComplete)
        Q_EMIT completeChanged();
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

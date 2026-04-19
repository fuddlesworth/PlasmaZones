// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedReal.h>

#include <PhosphorAnimation/MotionSpec.h>

namespace PhosphorAnimation {

PhosphorAnimatedReal::PhosphorAnimatedReal(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedReal::~PhosphorAnimatedReal() = default;

qreal PhosphorAnimatedReal::from() const
{
    return m_animatedValue.from();
}
qreal PhosphorAnimatedReal::to() const
{
    return m_animatedValue.to();
}
qreal PhosphorAnimatedReal::value() const
{
    return m_animatedValue.value();
}
bool PhosphorAnimatedReal::isAnimating() const
{
    return m_animatedValue.isAnimating();
}
bool PhosphorAnimatedReal::isComplete() const
{
    return m_animatedValue.isComplete();
}

bool PhosphorAnimatedReal::start(qreal from, qreal to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedReal::startImpl(qreal from, qreal to, IMotionClock* clock)
{
    if (!clock) {
        return false;
    }
    MotionSpec<qreal> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const qreal&) {
        Q_EMIT valueChanged();
    };
    spec.onComplete = [this]() {
        Q_EMIT animatingChanged();
        Q_EMIT completeChanged();
    };

    const bool ok = m_animatedValue.start(from, to, std::move(spec));
    // Emit fromChanged/toChanged unconditionally on start-call so QML
    // bindings resync even when the new segment collapses degenerate
    // (from ≈ to) and `start` returns false — the endpoint values
    // still got overwritten.
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedReal::retarget(qreal to)
{
    const bool ok = m_animatedValue.retarget(to);
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedReal::cancel()
{
    m_animatedValue.cancel();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedReal::finish()
{
    m_animatedValue.finish();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedReal::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedReal::onSync()
{
    // Per-frame tick from the window's beforeSynchronizing signal.
    // AnimatedValue::advance fires the spec's onValueChanged /
    // onComplete which emit our property-change signals.
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

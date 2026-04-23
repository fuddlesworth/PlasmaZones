// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedReal.h>

#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

namespace PhosphorAnimation {

PhosphorAnimatedReal::PhosphorAnimatedReal(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedReal::~PhosphorAnimatedReal()
{
    // Cancel first, THEN let the default-destruction path run. At
    // base-dtor time `m_animatedValue` has already been destroyed
    // (member destruction is reverse of declaration) — if the render-
    // thread `beforeSynchronizing` handler is mid-execution when the
    // GUI thread destroys us, it would otherwise UAF on the gone
    // AnimatedValue<T>. Cancelling here clears the running flag so
    // the next `onSync()` short-circuits, and disconnectSyncSignal()
    // in the base dtor drops the connection for future frames.
    m_animatedValue.cancel();
}

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
    return detail::startImpl(this, m_animatedValue, from, to, clock, profile().value());
}

bool PhosphorAnimatedReal::retarget(qreal to)
{
    return detail::retargetImpl(this, m_animatedValue, to);
}

void PhosphorAnimatedReal::cancel()
{
    detail::cancelImpl(this, m_animatedValue);
}

void PhosphorAnimatedReal::finish()
{
    detail::finishImpl(this, m_animatedValue);
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedPoint.h>

#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

namespace PhosphorAnimation {

PhosphorAnimatedPoint::PhosphorAnimatedPoint(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedPoint::~PhosphorAnimatedPoint()
{
    // See PhosphorAnimatedReal::~PhosphorAnimatedReal for rationale.
    m_animatedValue.cancel();
}

QPointF PhosphorAnimatedPoint::from() const
{
    return m_animatedValue.from();
}
QPointF PhosphorAnimatedPoint::to() const
{
    return m_animatedValue.to();
}
QPointF PhosphorAnimatedPoint::value() const
{
    return m_animatedValue.value();
}
bool PhosphorAnimatedPoint::isAnimating() const
{
    return m_animatedValue.isAnimating();
}
bool PhosphorAnimatedPoint::isComplete() const
{
    return m_animatedValue.isComplete();
}

bool PhosphorAnimatedPoint::start(const QPointF& from, const QPointF& to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedPoint::startImpl(const QPointF& from, const QPointF& to, IMotionClock* clock)
{
    return detail::startImpl(this, m_animatedValue, from, to, clock, profile().value());
}

bool PhosphorAnimatedPoint::retarget(const QPointF& to)
{
    return detail::retargetImpl(this, m_animatedValue, to);
}

void PhosphorAnimatedPoint::cancel()
{
    detail::cancelImpl(this, m_animatedValue);
}

void PhosphorAnimatedPoint::finish()
{
    detail::finishImpl(this, m_animatedValue);
}

void PhosphorAnimatedPoint::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedPoint::onSync()
{
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

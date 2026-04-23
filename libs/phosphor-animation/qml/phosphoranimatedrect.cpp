// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedRect.h>

#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

namespace PhosphorAnimation {

PhosphorAnimatedRect::PhosphorAnimatedRect(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedRect::~PhosphorAnimatedRect()
{
    // See PhosphorAnimatedReal::~PhosphorAnimatedReal for rationale.
    m_animatedValue.cancel();
}

QRectF PhosphorAnimatedRect::from() const
{
    return m_animatedValue.from();
}
QRectF PhosphorAnimatedRect::to() const
{
    return m_animatedValue.to();
}
QRectF PhosphorAnimatedRect::value() const
{
    return m_animatedValue.value();
}
bool PhosphorAnimatedRect::isAnimating() const
{
    return m_animatedValue.isAnimating();
}
bool PhosphorAnimatedRect::isComplete() const
{
    return m_animatedValue.isComplete();
}

bool PhosphorAnimatedRect::start(const QRectF& from, const QRectF& to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedRect::startImpl(const QRectF& from, const QRectF& to, IMotionClock* clock)
{
    return detail::startImpl(this, m_animatedValue, from, to, clock, profile().value());
}

bool PhosphorAnimatedRect::retarget(const QRectF& to)
{
    return detail::retargetImpl(this, m_animatedValue, to);
}

void PhosphorAnimatedRect::cancel()
{
    detail::cancelImpl(this, m_animatedValue);
}

void PhosphorAnimatedRect::finish()
{
    detail::finishImpl(this, m_animatedValue);
}

void PhosphorAnimatedRect::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedRect::onSync()
{
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

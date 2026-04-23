// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedSize.h>

#include <PhosphorAnimation/qml/detail/PhosphorAnimatedValueImpl.h>

namespace PhosphorAnimation {

PhosphorAnimatedSize::PhosphorAnimatedSize(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedSize::~PhosphorAnimatedSize()
{
    // See PhosphorAnimatedReal::~PhosphorAnimatedReal for rationale.
    m_animatedValue.cancel();
}

QSizeF PhosphorAnimatedSize::from() const
{
    return m_animatedValue.from();
}
QSizeF PhosphorAnimatedSize::to() const
{
    return m_animatedValue.to();
}
QSizeF PhosphorAnimatedSize::value() const
{
    return m_animatedValue.value();
}
bool PhosphorAnimatedSize::isAnimating() const
{
    return m_animatedValue.isAnimating();
}
bool PhosphorAnimatedSize::isComplete() const
{
    return m_animatedValue.isComplete();
}

bool PhosphorAnimatedSize::start(const QSizeF& from, const QSizeF& to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedSize::startImpl(const QSizeF& from, const QSizeF& to, IMotionClock* clock)
{
    return detail::startImpl(this, m_animatedValue, from, to, clock, profile().value());
}

bool PhosphorAnimatedSize::retarget(const QSizeF& to)
{
    return detail::retargetImpl(this, m_animatedValue, to);
}

void PhosphorAnimatedSize::cancel()
{
    detail::cancelImpl(this, m_animatedValue);
}

void PhosphorAnimatedSize::finish()
{
    detail::finishImpl(this, m_animatedValue);
}

void PhosphorAnimatedSize::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedSize::onSync()
{
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

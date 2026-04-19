// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedPoint.h>

#include <PhosphorAnimation/MotionSpec.h>

namespace PhosphorAnimation {

PhosphorAnimatedPoint::PhosphorAnimatedPoint(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedPoint::~PhosphorAnimatedPoint() = default;

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
    if (!clock) {
        return false;
    }
    MotionSpec<QPointF> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const QPointF&) {
        Q_EMIT valueChanged();
    };
    spec.onComplete = [this]() {
        Q_EMIT animatingChanged();
        Q_EMIT completeChanged();
    };

    const bool ok = m_animatedValue.start(from, to, std::move(spec));
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedPoint::retarget(const QPointF& to)
{
    const bool ok = m_animatedValue.retarget(to);
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedPoint::cancel()
{
    m_animatedValue.cancel();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedPoint::finish()
{
    m_animatedValue.finish();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
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

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
    // Check-before-emit — see PhosphorAnimatedReal::startImpl comment.
    const QPointF prevFrom = m_animatedValue.from();
    const QPointF prevTo = m_animatedValue.to();
    const QPointF prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();

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
    if (m_animatedValue.from() != prevFrom)
        Q_EMIT fromChanged();
    if (m_animatedValue.to() != prevTo)
        Q_EMIT toChanged();
    if (m_animatedValue.value() != prevValue)
        Q_EMIT valueChanged();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
        Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedPoint::retarget(const QPointF& to)
{
    const QPointF prevFrom = m_animatedValue.from();
    const QPointF prevTo = m_animatedValue.to();
    const QPointF prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    const bool ok = m_animatedValue.retarget(to);
    if (m_animatedValue.from() != prevFrom)
        Q_EMIT fromChanged();
    if (m_animatedValue.to() != prevTo)
        Q_EMIT toChanged();
    if (m_animatedValue.value() != prevValue)
        Q_EMIT valueChanged();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
        Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedPoint::cancel()
{
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.cancel();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
        Q_EMIT completeChanged();
}

void PhosphorAnimatedPoint::finish()
{
    const QPointF prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.finish();
    if (m_animatedValue.value() != prevValue)
        Q_EMIT valueChanged();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
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

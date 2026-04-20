// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedRect.h>

#include <PhosphorAnimation/MotionSpec.h>

namespace PhosphorAnimation {

PhosphorAnimatedRect::PhosphorAnimatedRect(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedRect::~PhosphorAnimatedRect() = default;

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
    if (!clock) {
        return false;
    }
    // Check-before-emit — see PhosphorAnimatedReal::startImpl comment.
    const QRectF prevFrom = m_animatedValue.from();
    const QRectF prevTo = m_animatedValue.to();
    const QRectF prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();

    MotionSpec<QRectF> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const QRectF&) {
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

bool PhosphorAnimatedRect::retarget(const QRectF& to)
{
    const QRectF prevFrom = m_animatedValue.from();
    const QRectF prevTo = m_animatedValue.to();
    const QRectF prevValue = m_animatedValue.value();
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

void PhosphorAnimatedRect::cancel()
{
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.cancel();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
        Q_EMIT completeChanged();
}

void PhosphorAnimatedRect::finish()
{
    const QRectF prevValue = m_animatedValue.value();
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

void PhosphorAnimatedRect::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedRect::onSync()
{
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

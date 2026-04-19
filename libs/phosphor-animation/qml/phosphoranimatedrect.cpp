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
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedRect::retarget(const QRectF& to)
{
    const bool ok = m_animatedValue.retarget(to);
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedRect::cancel()
{
    m_animatedValue.cancel();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedRect::finish()
{
    m_animatedValue.finish();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
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

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedSize.h>

#include <PhosphorAnimation/MotionSpec.h>

namespace PhosphorAnimation {

PhosphorAnimatedSize::PhosphorAnimatedSize(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedSize::~PhosphorAnimatedSize() = default;

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
    if (!clock) {
        return false;
    }
    // Check-before-emit — see PhosphorAnimatedReal::startImpl comment.
    const QSizeF prevFrom = m_animatedValue.from();
    const QSizeF prevTo = m_animatedValue.to();
    const QSizeF prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();

    MotionSpec<QSizeF> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const QSizeF&) {
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

bool PhosphorAnimatedSize::retarget(const QSizeF& to)
{
    const QSizeF prevFrom = m_animatedValue.from();
    const QSizeF prevTo = m_animatedValue.to();
    const QSizeF prevValue = m_animatedValue.value();
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

void PhosphorAnimatedSize::cancel()
{
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.cancel();
    if (m_animatedValue.isAnimating() != prevAnimating)
        Q_EMIT animatingChanged();
    if (m_animatedValue.isComplete() != prevComplete)
        Q_EMIT completeChanged();
}

void PhosphorAnimatedSize::finish()
{
    const QSizeF prevValue = m_animatedValue.value();
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

void PhosphorAnimatedSize::advance()
{
    m_animatedValue.advance();
}

void PhosphorAnimatedSize::onSync()
{
    m_animatedValue.advance();
}

} // namespace PhosphorAnimation

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
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedSize::retarget(const QSizeF& to)
{
    const bool ok = m_animatedValue.retarget(to);
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedSize::cancel()
{
    m_animatedValue.cancel();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedSize::finish()
{
    m_animatedValue.finish();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
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

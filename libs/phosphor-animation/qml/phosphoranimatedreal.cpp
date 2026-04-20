// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedReal.h>

#include <PhosphorAnimation/MotionSpec.h>

namespace PhosphorAnimation {

PhosphorAnimatedReal::PhosphorAnimatedReal(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedReal::~PhosphorAnimatedReal() = default;

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
    if (!clock) {
        return false;
    }
    // Snapshot pre-call state so we emit only the signals whose
    // underlying value actually changed (project rule: "Only emit
    // signals when value actually changes"). Pre-PR-344 every one of
    // these emitted unconditionally on every start() / retarget() /
    // cancel() / finish(), forcing QML binding re-eval up to 5× per
    // call even for no-op state flips.
    const qreal prevFrom = m_animatedValue.from();
    const qreal prevTo = m_animatedValue.to();
    const qreal prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();

    MotionSpec<qreal> spec;
    spec.profile = profile().value();
    spec.clock = clock;
    spec.onValueChanged = [this](const qreal&) {
        Q_EMIT valueChanged();
    };
    spec.onComplete = [this]() {
        Q_EMIT animatingChanged();
        Q_EMIT completeChanged();
    };

    const bool ok = m_animatedValue.start(from, to, std::move(spec));

    if (m_animatedValue.from() != prevFrom) {
        Q_EMIT fromChanged();
    }
    if (m_animatedValue.to() != prevTo) {
        Q_EMIT toChanged();
    }
    if (m_animatedValue.value() != prevValue) {
        Q_EMIT valueChanged();
    }
    if (m_animatedValue.isAnimating() != prevAnimating) {
        Q_EMIT animatingChanged();
    }
    if (m_animatedValue.isComplete() != prevComplete) {
        Q_EMIT completeChanged();
    }
    return ok;
}

bool PhosphorAnimatedReal::retarget(qreal to)
{
    const qreal prevFrom = m_animatedValue.from();
    const qreal prevTo = m_animatedValue.to();
    const qreal prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();

    const bool ok = m_animatedValue.retarget(to);

    if (m_animatedValue.from() != prevFrom) {
        Q_EMIT fromChanged();
    }
    if (m_animatedValue.to() != prevTo) {
        Q_EMIT toChanged();
    }
    if (m_animatedValue.value() != prevValue) {
        Q_EMIT valueChanged();
    }
    if (m_animatedValue.isAnimating() != prevAnimating) {
        Q_EMIT animatingChanged();
    }
    if (m_animatedValue.isComplete() != prevComplete) {
        Q_EMIT completeChanged();
    }
    return ok;
}

void PhosphorAnimatedReal::cancel()
{
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.cancel();
    if (m_animatedValue.isAnimating() != prevAnimating) {
        Q_EMIT animatingChanged();
    }
    if (m_animatedValue.isComplete() != prevComplete) {
        Q_EMIT completeChanged();
    }
}

void PhosphorAnimatedReal::finish()
{
    const qreal prevValue = m_animatedValue.value();
    const bool prevAnimating = m_animatedValue.isAnimating();
    const bool prevComplete = m_animatedValue.isComplete();
    m_animatedValue.finish();
    if (m_animatedValue.value() != prevValue) {
        Q_EMIT valueChanged();
    }
    if (m_animatedValue.isAnimating() != prevAnimating) {
        Q_EMIT animatingChanged();
    }
    if (m_animatedValue.isComplete() != prevComplete) {
        Q_EMIT completeChanged();
    }
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

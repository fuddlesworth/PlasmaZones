// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedColor.h>

#include <PhosphorAnimation/MotionSpec.h>

#include <QLoggingCategory>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcAnimatedColor, "phosphoranimation.qml.color")
}

PhosphorAnimatedColor::PhosphorAnimatedColor(QObject* parent)
    : PhosphorAnimatedValueBase(parent)
{
}

PhosphorAnimatedColor::~PhosphorAnimatedColor() = default;

// Dispatch accessor reading from whichever space is active — both
// AnimatedValue instances share endpoint / state on a best-effort
// basis (writes go to one, the other is dormant). The active-space
// getter keeps `value()` meaningful across space flips while idle.
QColor PhosphorAnimatedColor::from() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.from() : m_animatedValueLinear.from();
}
QColor PhosphorAnimatedColor::to() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.to() : m_animatedValueLinear.to();
}
QColor PhosphorAnimatedColor::value() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.value() : m_animatedValueLinear.value();
}

bool PhosphorAnimatedColor::isAnimating() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.isAnimating()
                                              : m_animatedValueLinear.isAnimating();
}
bool PhosphorAnimatedColor::isComplete() const
{
    return m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.isComplete() : m_animatedValueLinear.isComplete();
}

PhosphorAnimatedColor::ColorSpace PhosphorAnimatedColor::colorSpace() const
{
    return m_activeSpace;
}

void PhosphorAnimatedColor::setColorSpace(ColorSpace space)
{
    if (space == m_activeSpace) {
        return;
    }
    if (isAnimating()) {
        // Flipping space mid-animation would produce a visible
        // chromatic-path jump. Refuse the write and log once — rare
        // enough that per-instance flags aren't worth adding.
        qCDebug(lcAnimatedColor) << "setColorSpace ignored while animating — flip requires a quiesced animation";
        return;
    }
    m_activeSpace = space;
    Q_EMIT colorSpaceChanged();
}

bool PhosphorAnimatedColor::start(const QColor& from, const QColor& to)
{
    return startImpl(from, to, resolveClock());
}

bool PhosphorAnimatedColor::startImpl(const QColor& from, const QColor& to, IMotionClock* clock)
{
    if (!clock) {
        return false;
    }
    // Route through whichever space is active; emit our property-
    // change signals from the spec callbacks.
    auto onValueChanged = [this](const QColor&) {
        Q_EMIT valueChanged();
    };
    auto onComplete = [this]() {
        Q_EMIT animatingChanged();
        Q_EMIT completeChanged();
    };
    bool ok = false;
    if (m_activeSpace == ColorSpace::OkLab) {
        MotionSpec<QColor> spec;
        spec.profile = profile().value();
        spec.clock = clock;
        spec.onValueChanged = onValueChanged;
        spec.onComplete = onComplete;
        ok = m_animatedValueOkLab.start(from, to, std::move(spec));
    } else {
        MotionSpec<QColor> spec;
        spec.profile = profile().value();
        spec.clock = clock;
        spec.onValueChanged = onValueChanged;
        spec.onComplete = onComplete;
        ok = m_animatedValueLinear.start(from, to, std::move(spec));
    }
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

bool PhosphorAnimatedColor::retarget(const QColor& to)
{
    const bool ok =
        m_activeSpace == ColorSpace::OkLab ? m_animatedValueOkLab.retarget(to) : m_animatedValueLinear.retarget(to);
    Q_EMIT fromChanged();
    Q_EMIT toChanged();
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
    return ok;
}

void PhosphorAnimatedColor::cancel()
{
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.cancel();
    } else {
        m_animatedValueLinear.cancel();
    }
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedColor::finish()
{
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.finish();
    } else {
        m_animatedValueLinear.finish();
    }
    Q_EMIT valueChanged();
    Q_EMIT animatingChanged();
    Q_EMIT completeChanged();
}

void PhosphorAnimatedColor::advance()
{
    if (m_activeSpace == ColorSpace::OkLab) {
        m_animatedValueOkLab.advance();
    } else {
        m_animatedValueLinear.advance();
    }
}

void PhosphorAnimatedColor::onSync()
{
    advance();
}

} // namespace PhosphorAnimation

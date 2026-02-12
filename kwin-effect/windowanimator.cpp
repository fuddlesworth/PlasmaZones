// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <effect/effectwindow.h>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace PlasmaZones {

WindowAnimator::WindowAnimator(QObject* parent)
    : QObject(parent)
{
}

bool WindowAnimator::hasAnimation(KWin::EffectWindow* window) const
{
    return m_animations.contains(window);
}

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QRectF& startGeometry, const QRect& endGeometry)
{
    if (!window || !m_enabled) {
        return false;
    }

    // If geometry is the same, no animation needed
    if (startGeometry.toRect() == endGeometry) {
        return false;
    }

    WindowAnimation anim;
    anim.startGeometry = startGeometry;
    anim.endGeometry = QRectF(endGeometry);
    anim.duration = m_duration;
    anim.timer.start();
    m_animations[window] = anim;

    // Request repaint to start animation
    window->addRepaintFull();

    qCDebug(lcEffect) << "Started animation from" << startGeometry << "to" << endGeometry;
    return true;
}

void WindowAnimator::removeAnimation(KWin::EffectWindow* window)
{
    m_animations.remove(window);
}

void WindowAnimator::clear()
{
    m_animations.clear();
}

bool WindowAnimator::isAnimationComplete(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return true; // No animation = complete
    }
    return it->isComplete();
}

QRectF WindowAnimator::currentGeometry(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry() : QRectF();
    }
    return it->currentGeometry();
}

QRect WindowAnimator::finalGeometry(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRect();
    }
    return it->endGeometry.toRect();
}

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data)
{
    auto it = m_animations.find(window);
    if (it == m_animations.end()) {
        return;
    }

    const WindowAnimation& anim = *it;

    // Validate animation state
    if (!anim.isValid()) {
        m_animations.erase(it);
        return;
    }

    QRectF current = anim.currentGeometry();
    QRectF original = window->frameGeometry();

    // Calculate translation offset
    data += QPointF(current.x() - original.x(), current.y() - original.y());

    // Calculate scale factors with minimum threshold
    constexpr qreal MinDimension = 10.0;
    if (original.width() >= MinDimension && original.height() >= MinDimension) {
        data.setXScale(current.width() / original.width());
        data.setYScale(current.height() / original.height());
    }
}

bool WindowAnimator::isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return false;
    }
    return it->endGeometry.toRect() == targetGeometry;
}

void WindowAnimator::redirectAnimation(KWin::EffectWindow* window, const QRect& newTarget)
{
    auto it = m_animations.find(window);
    if (it == m_animations.end()) {
        return;
    }

    // Start new animation from current interpolated position
    WindowAnimation anim;
    anim.startGeometry = it->currentGeometry();
    anim.endGeometry = QRectF(newTarget);
    anim.duration = m_duration;
    anim.timer.start();
    *it = anim;

    qCDebug(lcEffect) << "Redirected animation from" << anim.startGeometry << "to" << newTarget;
}

} // namespace PlasmaZones

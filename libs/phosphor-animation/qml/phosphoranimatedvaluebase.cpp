// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorAnimatedValueBase.h>

#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <QtQuick/QQuickWindow>

namespace PhosphorAnimation {

PhosphorAnimatedValueBase::PhosphorAnimatedValueBase(QObject* parent)
    : QObject(parent)
{
}

PhosphorAnimatedValueBase::~PhosphorAnimatedValueBase() = default;

QQuickWindow* PhosphorAnimatedValueBase::window() const
{
    return m_window.data();
}

void PhosphorAnimatedValueBase::setWindow(QQuickWindow* w)
{
    if (m_window.data() == w) {
        return;
    }
    // Drop the previous window's sync hook before rebinding so a fast
    // rebind (re-parent an Item across windows) doesn't leave a stale
    // connection firing `onSync` for both windows' frame cycles.
    disconnectSyncSignal();
    m_window = w;
    if (m_window) {
        connectSyncSignal();
    }
    Q_EMIT windowChanged();
}

PhosphorProfile PhosphorAnimatedValueBase::profile() const
{
    return m_profile;
}

void PhosphorAnimatedValueBase::setProfile(const PhosphorProfile& p)
{
    if (m_profile == p) {
        return;
    }
    m_profile = p;
    Q_EMIT profileChanged();
}

IMotionClock* PhosphorAnimatedValueBase::resolveClock() const
{
    if (!m_window) {
        return nullptr;
    }
    return QtQuickClockManager::instance().clockFor(m_window.data());
}

void PhosphorAnimatedValueBase::connectSyncSignal()
{
    if (!m_window || m_syncConnection) {
        return;
    }
    // Qt::DirectConnection + beforeSynchronizing = GUI-thread tick.
    // The render thread is blocked during this signal per Qt's
    // contract, so NOTIFY signals emitted from inside `onSync` reach
    // the QML binding evaluator on the thread it expects.
    m_syncConnection = connect(
        m_window.data(), &QQuickWindow::beforeSynchronizing, this,
        [this]() {
            onSync();
        },
        Qt::DirectConnection);
}

void PhosphorAnimatedValueBase::disconnectSyncSignal()
{
    if (m_syncConnection) {
        QObject::disconnect(m_syncConnection);
        m_syncConnection = {};
    }
}

} // namespace PhosphorAnimation

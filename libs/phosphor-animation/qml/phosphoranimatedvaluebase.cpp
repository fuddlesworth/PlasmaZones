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

PhosphorAnimatedValueBase::~PhosphorAnimatedValueBase()
{
    // Drop both window-bound connections explicitly — at base dtor time
    // m_window may still be live and the subclass's AnimatedValue<T>
    // members have already been destroyed (member destruction order is
    // reverse of declaration, so the derived class's data is gone).
    // Disconnect before `~QObject` runs so no last-minute
    // `beforeSynchronizing` can race with a half-destroyed this.
    disconnectSyncSignal();
    disconnectDestroyedSignal();
}

QQuickWindow* PhosphorAnimatedValueBase::window() const
{
    return m_window.data();
}

void PhosphorAnimatedValueBase::setWindow(QQuickWindow* w)
{
    if (m_window.data() == w) {
        return;
    }
    // Drop both the previous window's sync hook and its destroyed hook
    // before rebinding. Without the explicit disconnect, a fast rebind
    // (re-parent an Item across windows) would leave stale connections
    // that fire `onSync` / `handleWindowDestroying` for the OLD
    // window's frame cycle and teardown respectively.
    disconnectSyncSignal();
    disconnectDestroyedSignal();
    m_window = w;
    if (m_window) {
        connectSyncSignal();
        connectDestroyedSignal();
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

void PhosphorAnimatedValueBase::connectDestroyedSignal()
{
    if (!m_window || m_destroyedConnection) {
        return;
    }
    // DirectConnection: the `destroyed` signal fires from the window's
    // destructor *on the thread that owns the window*. For QQuickWindow
    // this is always the GUI thread, so the lambda runs synchronously
    // and we get to cancel our animation BEFORE Qt's teardown drops
    // the QtQuickClock out from under us (the clock is owned by
    // QtQuickClockManager which evicts lazily — without this hook the
    // wrapper's AnimatedValue<T>::MotionSpec::clock could outlive the
    // QtQuickClock and UAF on the next tick).
    m_destroyedConnection = connect(
        m_window.data(), &QObject::destroyed, this,
        [this](QObject*) {
            handleWindowDestroying();
        },
        Qt::DirectConnection);
}

void PhosphorAnimatedValueBase::disconnectDestroyedSignal()
{
    if (m_destroyedConnection) {
        QObject::disconnect(m_destroyedConnection);
        m_destroyedConnection = {};
    }
}

void PhosphorAnimatedValueBase::handleWindowDestroying()
{
    // Drop the sync connection — we're about to lose the window, and
    // any pending beforeSynchronizing would crash if fired after this.
    disconnectSyncSignal();
    // Virtual — each typed subclass cancels its own AnimatedValue<T>.
    // Must complete BEFORE the QtQuickClock goes away because the
    // AnimatedValue::MotionSpec captured `clock` as a raw pointer.
    cancel();
    // Ordering requirement: clear the QPointer BEFORE emitting
    // windowChanged. The relative order of Qt's `destroyed`-signal
    // dispatch and QPointer's auto-clear is undefined — a reader in
    // `onWindowChanged` that inspects `window()` must see null, not a
    // stale non-null pointer that happens to still dereference
    // because the QObject destruction hasn't finished unwinding yet.
    // Explicit clear here guarantees observers get the same "window
    // is gone" view regardless of which signal-dispatch ordering Qt
    // chose this run.
    m_window.clear();
    Q_EMIT windowChanged();
}

} // namespace PhosphorAnimation

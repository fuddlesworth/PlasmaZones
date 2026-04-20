// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtQml/qqmlregistration.h>
// Full include needed (not forward-declare) because the
// Q_PROPERTY(QQuickWindow*) declaration requires a complete type for
// MOC's metatype registration — a pointer-to-incomplete-type trips
// Qt's static_assert on fully-defined pointed-to types.
#include <QtQuick/QQuickWindow>

namespace PhosphorAnimation {

class IMotionClock;

/**
 * @brief Shared base for the five per-T `PhosphorAnimated*` Q_OBJECTs.
 *
 * Phase 4 decision O refinement. Holds the cross-T plumbing —
 * `window` property (resolves clock through `QtQuickClockManager`),
 * `profile` property, lifecycle flags — so each typed subclass can
 * focus on its T-specific `from` / `to` / `value` surface and
 * `start` / `retarget` overloads. Not instantiable from QML
 * (registered `QML_UNCREATABLE`) — consumers pick
 * `PhosphorAnimatedReal` / `…Point` / `…Size` / `…Rect` / `…Color`.
 *
 * ## Auto-advance
 *
 * On `setWindow(window)`, the base connects to
 * `QQuickWindow::beforeSynchronizing` with `Qt::DirectConnection`.
 * That signal fires on the GUI thread (render thread blocked during
 * sync) once per frame — the natural tick granularity for an
 * animation. The connected slot calls the derived class's `onSync()`
 * which pumps `AnimatedValue<T>::advance` and emits whatever
 * property-change signals the tick produced.
 *
 * Deliberately NOT using `afterFrameEnd` / `frameSwapped` (render
 * thread) to avoid cross-thread signal dispatch into QML's property
 * binding evaluator (GUI-thread-only). `beforeSynchronizing` is
 * GUI-thread by Qt's contract.
 *
 * Tests can drive manually via `advance()` without a window bound —
 * the subclass pumps its `AnimatedValue<T>::advance()` directly, no
 * clock needed for the step (spec's `clock` must still be non-null
 * at `start()` time, so tests supply a TestClock via the public
 * `start()` overload's clock parameter — sub-commit-level test hook
 * covered in the subclass API).
 */
class PHOSPHORANIMATION_EXPORT PhosphorAnimatedValueBase : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedValueBase)
    QML_UNCREATABLE("Use PhosphorAnimatedReal / Point / Size / Rect / Color — the base is abstract.")

    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(PhosphorAnimation::PhosphorProfile profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(bool isAnimating READ isAnimating NOTIFY animatingChanged)
    Q_PROPERTY(bool isComplete READ isComplete NOTIFY completeChanged)

public:
    ~PhosphorAnimatedValueBase() override;

    QQuickWindow* window() const;
    void setWindow(QQuickWindow* w);

    PhosphorProfile profile() const;
    void setProfile(const PhosphorProfile& p);

    /// Subclass hook — returns the T-specific `AnimatedValue::isAnimating`.
    virtual bool isAnimating() const = 0;
    /// Subclass hook — returns the T-specific `AnimatedValue::isComplete`.
    virtual bool isComplete() const = 0;

    /// Subclass hook — cancels the T-specific AnimatedValue (no-op if
    /// not animating). Emits `animatingChanged` if the flag flipped.
    Q_INVOKABLE virtual void cancel() = 0;

    /// Subclass hook — force-completes the T-specific AnimatedValue,
    /// firing its onValueChanged + onComplete callbacks. Emits
    /// `valueChanged` / `animatingChanged` / `completeChanged` as
    /// appropriate.
    Q_INVOKABLE virtual void finish() = 0;

    /// Manual tick — calls the T-specific AnimatedValue::advance.
    /// Production auto-advance flows through `onSync` triggered by
    /// the window's `beforeSynchronizing` signal; this override lets
    /// tests and custom pumping drive the progression without a
    /// window bound.
    Q_INVOKABLE virtual void advance() = 0;

Q_SIGNALS:
    void windowChanged();
    void profileChanged();
    void animatingChanged();
    void completeChanged();

protected:
    explicit PhosphorAnimatedValueBase(QObject* parent = nullptr);

    /// Resolve the clock for the current `m_window`, or nullptr if no
    /// window is bound. Subclasses call this from their `start` to
    /// stamp the `MotionSpec::clock`.
    IMotionClock* resolveClock() const;

    /// Called once per frame on the GUI thread (via
    /// `beforeSynchronizing`) when a window is bound. Subclass pumps
    /// its `AnimatedValue<T>::advance()` and emits change signals.
    virtual void onSync() = 0;

private:
    void connectSyncSignal();
    void disconnectSyncSignal();
    void connectDestroyedSignal();
    void disconnectDestroyedSignal();
    void handleWindowDestroying();

    QPointer<QQuickWindow> m_window;
    PhosphorProfile m_profile;
    QMetaObject::Connection m_syncConnection;
    /// Window-destroyed hook. Fires synchronously on the GUI thread
    /// before the window is dropped — we cancel any in-flight animation
    /// so its `MotionSpec::clock` raw pointer doesn't outlive the
    /// clock that `QtQuickClockManager` evicts alongside the window.
    QMetaObject::Connection m_destroyedConnection;
};

} // namespace PhosphorAnimation

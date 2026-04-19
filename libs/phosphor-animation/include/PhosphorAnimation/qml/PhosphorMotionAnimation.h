// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtCore/QVariantAnimation>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief `QAbstractAnimation` subclass driving property animation with a
 *        phosphor-animation `Profile`.
 *
 * Phase 4 decision Q — the working shape for QML `Behavior` sites:
 *
 * ```qml
 * Behavior on opacity {
 *     PhosphorMotionAnimation { profile: "overlay.fade" }
 * }
 * ```
 *
 * Inherits from `QVariantAnimation` so Qt's animation framework drives
 * `updateCurrentTime` / `updateCurrentValue` through its existing
 * timer infrastructure. Behavior fills in `startValue` / `endValue`
 * from the wrapped property's old / new values; we override
 * `duration()` to come from the `Profile`, set `easingCurve` to
 * `Linear` so Qt's built-in easing is a no-op, and override
 * `interpolated()` to apply our Phase-3 `Curve::evaluate` + the
 * matching `Interpolate<T>::lerp` for each supported
 * `QVariant::typeId()` (qreal / QPointF / QSizeF / QRectF / QColor).
 *
 * ## Profile binding (decision R)
 *
 * `profile` is a `QVariant` accepting either:
 *
 *   - `QString` path — resolved live through `PhosphorProfileRegistry`.
 *     A settings-reload that calls `registerProfile(path, newProfile)`
 *     fires `profileChanged(path)`; this object re-resolves and
 *     rebinds, with subsequent `updateCurrentValue` ticks honouring
 *     the new curve / duration (new duration applies on the NEXT
 *     `start()` — mid-flight the old duration continues to avoid a
 *     visual stall).
 *   - `PhosphorProfile` value — compile-time snapshot, installed
 *     as-is. No registry indirection, no live update.
 *
 * Assigning an unresolved path string (the registry doesn't know
 * about it yet — typical during startup before `ProfileLoader`
 * has scanned the XDG dirs) keeps the animation running with the
 * library default profile (OutCubic, 150 ms) until the registry
 * gets populated; the `profileChanged(path)` signal will then
 * trigger the rebind.
 *
 * ## Spring curves caveat
 *
 * Qt's `QVariantAnimation` is fundamentally fixed-duration — it
 * parameterises progress as `t ∈ [0, 1]` and calls `interpolated` with
 * Qt-driven easing. Stateful curves (`Spring`) need real-time `dt`
 * integration to preserve velocity across retargets, which the
 * QPropertyAnimation/Behavior path cannot provide.
 *
 * PhosphorMotionAnimation therefore uses the spring's **analytical
 * step response** (`Spring::evaluate(t)`) rather than the physics
 * integration (`Spring::step(dt, state, target)`). The visual shape
 * is still spring-like — overshoot preserved, settle-time derived
 * from damping ratio — but velocity continuity across mid-animation
 * retargets is lost. Consumers that need true velocity-preserving
 * spring retargets in QML should use `PhosphorAnimatedReal` (et al.)
 * from sub-commit 3, which uses the Phase-3 `AnimatedValue<T>` path
 * directly and preserves the physics.
 *
 * This is a tractable tradeoff: Behavior integration is the 90 %
 * use case (fixed-duration UI fades, slides, size changes), and the
 * 10 % use case (gesture-driven spring retargets) has a clearly-
 * documented alternative.
 */
class PHOSPHORANIMATION_EXPORT PhosphorMotionAnimation : public QVariantAnimation
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorMotionAnimation)

    /// Either a `QString` path (resolved live via `PhosphorProfileRegistry`)
    /// or a `PhosphorProfile` value (compile-time snapshot). See class
    /// doc for the dispatch shape.
    Q_PROPERTY(QVariant profile READ profile WRITE setProfile NOTIFY profileChanged)

public:
    explicit PhosphorMotionAnimation(QObject* parent = nullptr);
    ~PhosphorMotionAnimation() override;

    QVariant profile() const;
    void setProfile(const QVariant& p);

    /// Current effective profile (with library defaults filled in for
    /// unset fields). Exposed for tests and adapter code that needs to
    /// inspect the resolved snapshot — not a Q_PROPERTY because the
    /// settings-time target for QML is always the input `profile`
    /// property, not the resolved form.
    const Profile& resolvedProfile() const;

    /// `QAbstractAnimation::duration()` — driven by the resolved
    /// profile's `effectiveDuration`. Qt queries this at `start()` time;
    /// a profile change during a running animation updates the cached
    /// value for the NEXT start, mid-flight runs keep their original
    /// duration to avoid visual stalls.
    int duration() const override;

Q_SIGNALS:
    void profileChanged();

protected:
    /// `QVariantAnimation` hook — called on each animation tick with
    /// the eased progress value. We bypass Qt's easing (it's set to
    /// Linear in the ctor) and apply our Phase-3 Curve + `Interpolate<T>::lerp`
    /// for the supported QVariant types.
    QVariant interpolated(const QVariant& from, const QVariant& to, qreal progress) const override;

private:
    void resolveFromVariant(const QVariant& p);
    void rebindToRegistryPath(const QString& path);
    void disconnectRegistrySignal();
    void applyResolvedProfile(const Profile& p);

    QVariant m_profile; ///< The QML-facing input: QString or PhosphorProfile.
    Profile m_resolvedProfile; ///< Effective value used by duration() / interpolated.
    QString m_boundPath; ///< Non-empty when the input was a path string — drives live-rebind.
    QMetaObject::Connection m_registryChangedConnection;
    QMetaObject::Connection m_registryReloadedConnection;
};

} // namespace PhosphorAnimation

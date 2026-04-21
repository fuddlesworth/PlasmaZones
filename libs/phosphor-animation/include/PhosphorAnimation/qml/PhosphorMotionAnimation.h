// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorProfile.h>

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtQml/qqmlregistration.h>
// Private header — project depends on qt6-qtdeclarative-devel which
// ships the private headers on Fedora (qt6-qtdeclarative-private-devel
// is a separate package on some distros but is pulled in by -devel on
// Fedora 42+).
#include <QtQuick/private/qquickanimation_p.h>

namespace PhosphorAnimation {

/**
 * @brief `QQuickPropertyAnimation` subclass driving property animation
 *        with a phosphor-animation `Profile`.
 *
 * Phase 4 decision Q — the working shape for QML `Behavior` sites:
 *
 * ```qml
 * Behavior on opacity {
 *     PhosphorMotionAnimation { profile: "overlay.fade" }
 * }
 * ```
 *
 * Inherits from `QQuickPropertyAnimation` (the Qt Quick base class that
 * QML `Behavior` wires into). On profile resolution, the Phase-3 curve
 * is converted to a `QEasingCurve::BezierSpline` approximation and
 * installed via `setEasing()`, and the profile duration is installed
 * via `setDuration()`. Qt Quick's animation infrastructure then handles
 * all timing, interpolation, and property writes — no per-tick
 * `interpolated()` override is needed.
 *
 * The BezierSpline approximation samples the Phase-3 `Curve::evaluate(t)`
 * into 16 piecewise cubic Bezier segments. For simple cubic-bezier
 * curves (4-parameter) this overshoots but the cost is negligible.
 * For springs/elastics, 16 segments gives sub-pixel accuracy at 60fps.
 *
 * ## Profile binding (decision R)
 *
 * `profile` is a `QVariant` accepting either:
 *
 *   - `QString` path — resolved live through `PhosphorProfileRegistry`.
 *     A settings-reload that calls `registerProfile(path, newProfile)`
 *     fires `profileChanged(path)`; this object re-resolves and
 *     rebinds, updating the easing curve and duration for the NEXT
 *     animation start.
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
 * `QQuickPropertyAnimation` is fundamentally fixed-duration — it
 * parameterises progress as `t in [0, 1]`. Stateful curves (`Spring`)
 * need real-time `dt` integration to preserve velocity across retargets,
 * which the QPropertyAnimation/Behavior path cannot provide.
 *
 * PhosphorMotionAnimation therefore uses the spring's **analytical
 * step response** (`Spring::evaluate(t)`) rather than the physics
 * integration (`Spring::step(dt, state, target)`). The visual shape
 * is still spring-like — overshoot preserved, settle-time derived
 * from damping ratio — but velocity continuity across mid-animation
 * retargets is lost. Consumers that need true velocity-preserving
 * spring retargets in QML should use `PhosphorAnimatedReal` (et al.)
 * which use the Phase-3 `AnimatedValue<T>` path directly and preserve
 * the physics.
 */
class PHOSPHORANIMATION_EXPORT PhosphorMotionAnimation : public QQuickPropertyAnimation
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

Q_SIGNALS:
    void profileChanged();

private:
    void resolveFromVariant(const QVariant& p);
    void rebindToRegistryPath(const QString& path);
    void disconnectRegistrySignal();
    void applyResolvedProfile(const Profile& p);

    /// Convert the resolved Profile's curve + duration into
    /// QQuickPropertyAnimation's native `easing` and `duration`
    /// properties. The curve is sampled into a piecewise cubic
    /// BezierSpline with 16 segments — sub-pixel accurate at 60fps
    /// for all supported curve types (springs, elastics, bounces).
    void applyResolvedEasing();

    QVariant m_profile; ///< The QML-facing input: QString or PhosphorProfile.
    Profile m_resolvedProfile; ///< Effective value used by easing/duration.
    QString m_boundPath; ///< Non-empty when the input was a path string — drives live-rebind.
    QMetaObject::Connection m_registryChangedConnection;
    QMetaObject::Connection m_registryReloadedConnection;
};

} // namespace PhosphorAnimation

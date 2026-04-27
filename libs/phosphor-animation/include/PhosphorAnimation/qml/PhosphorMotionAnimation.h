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
// Private Qt header — DELIBERATE dependency, not incidental. The
// `QQuickPropertyAnimation` base class lives under `QtQuick/private/*`
// upstream; no public alias exists. This is the same header every
// `Behavior`-wired property-animation subclass in Qt Quick uses
// internally, so its shape is far more stable than a typical private
// header — but Qt does not guarantee source-compat across MINOR Qt
// versions.
//
// Supported Qt range for this library:
//   - Minimum:   Qt 6.5 (`QQuickPropertyAnimation` shape we rely on
//                 has been stable since 6.5).
//   - Tested:    Qt 6.9, 6.10, 6.11 (the segment cap at
//                `kBezierSplineSegments` specifically guards 6.11;
//                older versions don't heap-corrupt on the 11+
//                segment boundary, but the cap is harmless there).
//   - Known-bad: Qt 6.11.0-6.11.x pre-patch — `QQuickPropertyAnimation::
//                setEasing` heap-corrupts on `QEasingCurve::BezierSpline`
//                curves with >=11 segments. Workaround in-library; no
//                fix required at the consumer site.
//
// Packaging notes: on Fedora, `qt6-qtdeclarative-devel` pulls the
// private headers via `-devel`. On Debian/Ubuntu the headers live
// under `qt6-declarative-private-dev`. Arch ships them in
// `qt6-declarative`. Distributions that split out private headers
// into a separate package (some openSUSE layouts) need the explicit
// `-private-devel` install.
//
// When bumping the Qt minimum: re-verify the `setEasing` boundary
// and update both `kBezierSplineSegments` and this comment if Qt's
// behaviour changes.
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
 * into `kBezierSplineSegments` piecewise cubic Bezier segments (8 by
 * default — chosen to stay safely below Qt 6.11's `setEasing` heap-
 * corruption threshold at ≥11 segments while keeping visual faithfulness
 * at typical 150-300 ms UI durations). Plain cubic-bezier `Easing`
 * profiles take a fast path that installs a single canonical segment
 * with the two original control points — no sampling.
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

    /// Override the resolved profile's duration (milliseconds). When
    /// `> 0`, this value is installed via `QQuickPropertyAnimation::
    /// setDuration` instead of the profile's own duration — the
    /// profile's curve is still used. Zero or negative means "use the
    /// profile's duration unchanged".
    ///
    /// The intended use is binding a theme-scaled value like
    /// `Kirigami.Units.longDuration` so a shared profile JSON
    /// provides the curve shape while the caller's theme-scaled
    /// duration drives the timing. Without this property, switching
    /// a Behavior off a hardcoded `duration: Kirigami.Units.*` onto
    /// a fixed-duration profile silently drops the user's animation-
    /// speed preference from Plasma's system settings.
    Q_PROPERTY(int durationOverride READ durationOverride WRITE setDurationOverride NOTIFY durationOverrideChanged)

public:
    /// Number of piecewise cubic Bezier segments used to approximate
    /// parametric / stateful curves (Spring, Elastic, Bounce,
    /// user-authored). Must stay below 11 — Qt 6.11's
    /// `QQuickPropertyAnimation::setEasing` heap-corrupts on
    /// `QEasingCurve::BezierSpline` curves with ≥11 segments. 8 keeps
    /// a safe margin while giving visually faithful approximations at
    /// 150-300 ms durations. Plain cubic-bezier `Easing` curves take
    /// the fast path (single canonical segment) and are not subject
    /// to this cap.
    ///
    /// Exposed as a public static so tests can assert the installed
    /// easing's segment count stays under the Qt boundary — see the
    /// BezierSpline regression test in
    /// test_phosphormotionanimation.cpp.
    static constexpr int kBezierSplineSegments = 8;

    explicit PhosphorMotionAnimation(QObject* parent = nullptr);
    ~PhosphorMotionAnimation() override;

    QVariant profile() const;
    void setProfile(const QVariant& p);

    int durationOverride() const;
    void setDurationOverride(int ms);

    /// Current effective profile (with library defaults filled in for
    /// unset fields). Exposed for tests and adapter code that needs to
    /// inspect the resolved snapshot — not a Q_PROPERTY because the
    /// settings-time target for QML is always the input `profile`
    /// property, not the resolved form.
    const Profile& resolvedProfile() const;

Q_SIGNALS:
    void profileChanged();
    void durationOverrideChanged();

private:
    void resolveFromVariant(const QVariant& p);
    void rebindToRegistryPath(const QString& path);
    void disconnectRegistrySignal();
    void applyResolvedProfile(const Profile& p);

    /// Convert the resolved Profile's curve + duration into
    /// QQuickPropertyAnimation's native `easing` and `duration`
    /// properties. Plain cubic-bezier `Easing` profiles install as a
    /// single canonical segment (exact round-trip); parametric /
    /// stateful curves are sampled into `kBezierSplineSegments`
    /// piecewise cubic Bezier segments.
    void applyResolvedEasing();

    QVariant m_profile; ///< The QML-facing input: QString or PhosphorProfile.
    Profile m_resolvedProfile; ///< Effective value used by easing/duration.
    QString m_boundPath; ///< Non-empty when the input was a path string — drives live-rebind.
    int m_durationOverride = 0; ///< When > 0, overrides the profile's duration.
    QMetaObject::Connection m_registryChangedConnection;
    QMetaObject::Connection m_registryReloadedConnection;
};

} // namespace PhosphorAnimation

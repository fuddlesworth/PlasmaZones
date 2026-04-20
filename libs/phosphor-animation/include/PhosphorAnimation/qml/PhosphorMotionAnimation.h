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
    /// profile's `effectiveDuration`, but SNAPSHOTTED at `start()`
    /// so that a profile swap mid-flight (live settings edit) cannot
    /// rewind the animation. Qt's animation framework reads `duration()`
    /// on every tick to compute progress = elapsed/duration; letting
    /// the live value leak through would turn a `200 ms → 400 ms` edit
    /// at elapsed=150 ms into progress=0.375 (a visible backwards
    /// jump) instead of the expected 0.75 continuing smoothly to 1.0.
    /// The new duration takes effect on the next `start()`.
    int duration() const override;

    /// Shadow QVariantAnimation::setEasingCurve to REJECT anything
    /// other than Linear. Phase-3 curves are applied by `interpolated()`
    /// directly; Qt's easing must stay at Linear to avoid double-easing
    /// (Qt's curve composed with ours). A consumer that writes
    /// `easingCurve: Easing.OutQuad` on a PhosphorMotionAnimation
    /// instance would otherwise silently re-introduce the double-easing
    /// bug — this wrapper logs and ignores non-Linear writes.
    ///
    /// Note: `QVariantAnimation::setEasingCurve` is NOT virtual, so
    /// this is a name-hiding override (static dispatch only). Callers
    /// that go through the base class's pointer or the Q_PROPERTY
    /// system may still bypass this guard; for those paths we catch
    /// drift by re-asserting Linear in `interpolated()` paths via
    /// the cached profile curve. Direct C++ `setEasingCurve` calls —
    /// the common mistake — are caught here.
    void setEasingCurve(const QEasingCurve& curve);

Q_SIGNALS:
    void profileChanged();

protected:
    /// `QVariantAnimation` hook — called on each animation tick with
    /// the eased progress value. We bypass Qt's easing (it's set to
    /// Linear in the ctor) and apply our Phase-3 Curve + `Interpolate<T>::lerp`
    /// for the supported QVariant types.
    QVariant interpolated(const QVariant& from, const QVariant& to, qreal progress) const override;

    /// `QAbstractAnimation::updateState` override — snapshots the
    /// resolved profile's duration into `m_activeDurationMs` on the
    /// `Stopped → Running` edge so mid-flight profile swaps can't
    /// rewind progress. Cache cleared on `Running → Stopped`.
    void updateState(QAbstractAnimation::State newState, QAbstractAnimation::State oldState) override;

private:
    void resolveFromVariant(const QVariant& p);
    void rebindToRegistryPath(const QString& path);
    void disconnectRegistrySignal();
    void applyResolvedProfile(const Profile& p);

    QVariant m_profile; ///< The QML-facing input: QString or PhosphorProfile.
    Profile m_resolvedProfile; ///< Effective value used by duration() / interpolated.
    int m_activeDurationMs = -1; ///< Frozen at start(); -1 while idle (fall through to resolved).
    QString m_boundPath; ///< Non-empty when the input was a path string — drives live-rebind.
    QMetaObject::Connection m_registryChangedConnection;
    QMetaObject::Connection m_registryReloadedConnection;
};

} // namespace PhosphorAnimation

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimationQml/phosphoranimationqml_export.h>
#include <PhosphorAnimationQml/PhosphorAnimatedValueBase.h>

#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML-facing animated `qreal` value.
 *
 * Instantiable from QML as `PhosphorAnimatedReal`. Wraps
 * `AnimatedValue<qreal>` — shares the Phase-3 progression semantics
 * (curve dispatch, retarget, safety cap, NaN gates) and the Phase-4
 * plumbing (window, clock manager, profile) inherited from
 * `PhosphorAnimatedValueBase`.
 *
 * ## QML usage
 *
 * ```qml
 * PhosphorAnimatedReal {
 *     id: fade
 *     window: Window.window
 *     profile: PhosphorProfile {
 *         curve: PhosphorCurve.fromSpring(PhosphorSpring.snappy())
 *     }
 * }
 * Rectangle { opacity: fade.value }
 * Button { onClicked: fade.start(0.0, 1.0) }
 * ```
 */
class PHOSPHORANIMATIONQML_EXPORT PhosphorAnimatedReal : public PhosphorAnimatedValueBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedReal)

    Q_PROPERTY(qreal from READ from NOTIFY fromChanged)
    Q_PROPERTY(qreal to READ to NOTIFY toChanged)
    Q_PROPERTY(qreal value READ value NOTIFY valueChanged)

public:
    explicit PhosphorAnimatedReal(QObject* parent = nullptr);
    ~PhosphorAnimatedReal() override;

    qreal from() const;
    qreal to() const;
    qreal value() const;

    bool isAnimating() const override;
    bool isComplete() const override;

    Q_INVOKABLE bool start(qreal from, qreal to);
    Q_INVOKABLE bool retarget(qreal to);
    void cancel() override;
    void finish() override;
    void advance() override;

Q_SIGNALS:
    void fromChanged();
    void toChanged();
    void valueChanged();

protected:
    void onSync() override;

private:
    /// Shared `start` helper — builds the MotionSpec, wires the
    /// per-type onValueChanged/onComplete callbacks into property-
    /// change emissions, and hands off to `AnimatedValue<qreal>::start`.
    bool startImpl(qreal from, qreal to, IMotionClock* clock);

    AnimatedValue<qreal> m_animatedValue;
};

} // namespace PhosphorAnimation

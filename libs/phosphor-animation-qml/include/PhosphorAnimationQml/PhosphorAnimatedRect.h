// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimationQml/phosphoranimationqml_export.h>
#include <PhosphorAnimationQml/PhosphorAnimatedValueBase.h>

#include <QtCore/QRectF>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML-facing animated `QRectF` value.
 * See `PhosphorAnimatedReal` for the shared usage contract.
 *
 * NB: retarget via `PreserveVelocity` on QRectF has the 4-D norm
 * unit-mixing caveat (see `RetargetPolicy` docs). For size-dominated
 * animations, pass an explicit policy via the C++ API or use separate
 * `PhosphorAnimatedPoint` + `PhosphorAnimatedSize` instances.
 */
class PHOSPHORANIMATIONQML_EXPORT PhosphorAnimatedRect : public PhosphorAnimatedValueBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedRect)

    Q_PROPERTY(QRectF from READ from NOTIFY fromChanged)
    Q_PROPERTY(QRectF to READ to NOTIFY toChanged)
    Q_PROPERTY(QRectF value READ value NOTIFY valueChanged)

public:
    explicit PhosphorAnimatedRect(QObject* parent = nullptr);
    ~PhosphorAnimatedRect() override;

    QRectF from() const;
    QRectF to() const;
    QRectF value() const;

    bool isAnimating() const override;
    bool isComplete() const override;

    Q_INVOKABLE bool start(const QRectF& from, const QRectF& to);
    Q_INVOKABLE bool retarget(const QRectF& to);
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
    bool startImpl(const QRectF& from, const QRectF& to, IMotionClock* clock);

    AnimatedValue<QRectF> m_animatedValue;
};

} // namespace PhosphorAnimation

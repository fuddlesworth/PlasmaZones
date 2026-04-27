// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedValueBase.h>

#include <QtCore/QPointF>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML-facing animated `QPointF` value.
 * See `PhosphorAnimatedReal` for the shared usage contract.
 */
class PHOSPHORANIMATION_EXPORT PhosphorAnimatedPoint : public PhosphorAnimatedValueBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedPoint)

    Q_PROPERTY(QPointF from READ from NOTIFY fromChanged)
    Q_PROPERTY(QPointF to READ to NOTIFY toChanged)
    Q_PROPERTY(QPointF value READ value NOTIFY valueChanged)

public:
    explicit PhosphorAnimatedPoint(QObject* parent = nullptr);
    ~PhosphorAnimatedPoint() override;

    QPointF from() const;
    QPointF to() const;
    QPointF value() const;

    bool isAnimating() const override;
    bool isComplete() const override;

    Q_INVOKABLE bool start(const QPointF& from, const QPointF& to);
    Q_INVOKABLE bool retarget(const QPointF& to);
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
    bool startImpl(const QPointF& from, const QPointF& to, IMotionClock* clock);

    AnimatedValue<QPointF> m_animatedValue;
};

} // namespace PhosphorAnimation

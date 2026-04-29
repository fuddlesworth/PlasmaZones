// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimationQml/phosphoranimationqml_export.h>
#include <PhosphorAnimationQml/PhosphorAnimatedValueBase.h>

#include <QtCore/QSizeF>
#include <QtQml/qqmlregistration.h>

namespace PhosphorAnimation {

/**
 * @brief QML-facing animated `QSizeF` value.
 * See `PhosphorAnimatedReal` for the shared usage contract.
 */
class PHOSPHORANIMATIONQML_EXPORT PhosphorAnimatedSize : public PhosphorAnimatedValueBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedSize)

    Q_PROPERTY(QSizeF from READ from NOTIFY fromChanged)
    Q_PROPERTY(QSizeF to READ to NOTIFY toChanged)
    Q_PROPERTY(QSizeF value READ value NOTIFY valueChanged)

public:
    explicit PhosphorAnimatedSize(QObject* parent = nullptr);
    ~PhosphorAnimatedSize() override;

    QSizeF from() const;
    QSizeF to() const;
    QSizeF value() const;

    bool isAnimating() const override;
    bool isComplete() const override;

    Q_INVOKABLE bool start(const QSizeF& from, const QSizeF& to);
    Q_INVOKABLE bool retarget(const QSizeF& to);
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
    bool startImpl(const QSizeF& from, const QSizeF& to, IMotionClock* clock);

    AnimatedValue<QSizeF> m_animatedValue;
};

} // namespace PhosphorAnimation

// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/Interpolate.h>
#include <PhosphorAnimation/phosphoranimation_export.h>
#include <PhosphorAnimation/qml/PhosphorAnimatedValueBase.h>

#include <QtGui/QColor>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace PhosphorAnimation {

/**
 * @brief QML-facing animated `QColor` value.
 *
 * Phase 4 decision P: `colorSpace` is a runtime enum property on the
 * wrapper. The underlying template parameter
 * `AnimatedValue<QColor, Space>` maps to a runtime branch inside the
 * wrapper — one compiled instance, two runtime paths.
 *
 * ```qml
 * PhosphorAnimatedColor {
 *     id: fade
 *     window: Window.window
 *     colorSpace: PhosphorAnimatedColor.OkLab
 *     profile: someProfile
 * }
 * Rectangle { color: fade.value }
 * Button { onClicked: fade.start("red", "blue") }
 * ```
 *
 * `colorSpace` is writable only while not animating — flipping the
 * space mid-animation would produce a visible jump as the midpoint
 * colour shifts between the two gamut paths. Writes attempted while
 * `isAnimating` is true are ignored (no-op, logs a debug hint).
 */
class PHOSPHORANIMATION_EXPORT PhosphorAnimatedColor : public PhosphorAnimatedValueBase
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PhosphorAnimatedColor)

    Q_PROPERTY(QColor from READ from NOTIFY fromChanged)
    Q_PROPERTY(QColor to READ to NOTIFY toChanged)
    Q_PROPERTY(QColor value READ value NOTIFY valueChanged)
    Q_PROPERTY(ColorSpace colorSpace READ colorSpace WRITE setColorSpace NOTIFY colorSpaceChanged)

public:
    /// Parallel to `PhosphorAnimation::ColorSpace` (defined in
    /// Interpolate.h). Integer values kept identical for `static_cast`
    /// round-trip — matches the decision-O convention used by
    /// `PhosphorEasing::Type` / `PhosphorProfile::SequenceMode`.
    enum class ColorSpace : int {
        Linear = int(PhosphorAnimation::ColorSpace::Linear),
        OkLab = int(PhosphorAnimation::ColorSpace::OkLab),
    };
    Q_ENUM(ColorSpace)

    explicit PhosphorAnimatedColor(QObject* parent = nullptr);
    ~PhosphorAnimatedColor() override;

    QColor from() const;
    QColor to() const;
    QColor value() const;

    ColorSpace colorSpace() const;
    void setColorSpace(ColorSpace space);

    bool isAnimating() const override;
    bool isComplete() const override;

    Q_INVOKABLE bool start(const QColor& from, const QColor& to);
    Q_INVOKABLE bool retarget(const QColor& to);
    void cancel() override;
    void finish() override;
    void advance() override;

Q_SIGNALS:
    void fromChanged();
    void toChanged();
    void valueChanged();
    void colorSpaceChanged();

protected:
    void onSync() override;

private:
    bool startImpl(const QColor& from, const QColor& to, IMotionClock* clock);

    /// Current-space AnimatedValue accessor helpers — keep the active-
    /// instance dispatch in one place so the public from/to/value
    /// readers read "live" state through a single ternary rather than
    /// duplicating it across three methods.
    QColor activeFrom() const;
    QColor activeTo() const;
    QColor activeValue() const;

    /// The runtime-dispatched animation core. `variant`-style: one of
    /// Linear or OkLab is active; swapping via setColorSpace replaces
    /// the active member while both are kept as fields to simplify
    /// the state-machine (and avoid a shared_ptr for no gain at this
    /// scale). `m_activeSpace` selects which one routes `start` /
    /// `advance` calls. `setColorSpace` propagates idle state across
    /// the flip via `AnimatedValue::seedFrom` so post-flip reads
    /// through `from()/to()/value()` stay continuous even though the
    /// two instances are otherwise independent.
    AnimatedValue<QColor, PhosphorAnimation::ColorSpace::Linear> m_animatedValueLinear;
    AnimatedValue<QColor, PhosphorAnimation::ColorSpace::OkLab> m_animatedValueOkLab;
    ColorSpace m_activeSpace = ColorSpace::Linear;
};

} // namespace PhosphorAnimation

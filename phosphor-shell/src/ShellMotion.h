// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/QtQuickClockManager.h>

#include <QObject>
#include <QString>
#include <QVariant>

#include <optional>

QT_BEGIN_NAMESPACE
class QDBusVariant;
QT_END_NAMESPACE

namespace PhosphorShellApp {

// The shell's motion root: the process-wide profile registry behind every
// `PhosphorMotionAnimation { profile: "shell.<name>" }` in the chrome, and
// the session's reduced-motion preference. Exposed to QML as the
// `ShellMotion` context property (the ShellEffects precedent); shell.qml
// binds `Motion.reducedMotion` to `reducedMotion`.
//
// Registry: the settings app and the editor build theirs through
// AnimationBootstrap, which also runs the JSON loaders for user-authored
// profiles. The shell owns only the primitives the identity names (A1
// §3.1), so it constructs the registries directly and registers those
// under the seed owner tag; a user JSON at the same path still wins
// through the registry's two-layer resolve. Must outlive the QML engine:
// Behavior bindings keep registry handles.
//
// Reduced motion: read from the settings portal (org.freedesktop.portal
// .Settings) so it follows the session whatever desktop hosts it, with
// `PHOSPHOR_REDUCED_MOTION=1` as the override for a harness. KDE exposes
// the animation speed as `org.kde.kdeglobals.KDE / AnimationDurationFactor`
// (0 means off); GNOME as `org.gnome.desktop.interface / enable-animations`.
// Both keys are watched for live changes.
class ShellMotion : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged)

public:
    explicit ShellMotion(QObject* parent = nullptr);
    ~ShellMotion() override;

    [[nodiscard]] bool reducedMotion() const;

    /// The registered profile paths, for the host and for tests.
    static QString settlePath();

    [[nodiscard]] PhosphorAnimation::PhosphorProfileRegistry* profileRegistry();
    [[nodiscard]] PhosphorAnimation::CurveRegistry* curveRegistry();

    /// Publish the registries and the clock manager as the QML defaults.
    /// Idempotent; `unpublish` clears them (call before the engine dies).
    void publish();
    void unpublish();

    /// Interpret one portal setting. Returns the reduced-motion verdict it
    /// implies, or an empty optional when the key is not a motion key.
    static std::optional<bool> reducedMotionFor(const QString& ns, const QString& key, const QVariant& value);

Q_SIGNALS:
    void reducedMotionChanged();

private Q_SLOTS:
    void onSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);

private:
    void registerProfiles();
    void readPortal();
    void setReducedMotion(bool reduced);

    PhosphorAnimation::CurveRegistry m_curves;
    PhosphorAnimation::PhosphorProfileRegistry m_profiles;
    PhosphorAnimation::QtQuickClockManager m_clocks;
    bool m_reducedMotion = false;
    bool m_published = false;
};

} // namespace PhosphorShellApp
